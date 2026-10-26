/* Own bounded D3DX bridge; no implementation from a Windows DLL. */
#include "fnv_sdk_runtime.h"
#include "windows_d3dx.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include <string.h>
#include <math.h>
#include <fenv.h>
static unsigned char *checked(uint32_t at,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,64):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(vm,&backing,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+64>base && !fnv_vm_access(vm,at,64,write)))p=NULL;}
    if(!p)recomp_unimpl("D3DX matrix memory unavailable",at);
    return p;
}
#include "dds_image_info.h"
static unsigned char *image_pointer(uint32_t at,uint32_t bytes,int write){
    unsigned char *data=at?fnv_cpu_try_memory(at,bytes):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(vm,&backing,&base,&size) || ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))data=NULL;}
    if(!data)recomp_unimpl("D3DX image argument memory unavailable",at);
    return data;
}
static void api_image_info_memory(void){
    uint32_t src=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2);
    if(!src || !bytes){fnv_cpu_return_u32(0x8876086cu,12);return;}
    /* Native D3DX accepts a NULL info output without inspecting source bytes. */
    if(!out){fnv_cpu_return_u32(0,12);return;}
    unsigned char *data=image_pointer(src,bytes,0),value[28];if(!data)return;
    uint32_t result=fnv_dds_image_info(data,bytes,value);
    if(result==FNV_DDS_UNSUPPORTED){recomp_unimpl("D3DX DDS image format or shape unsupported",src);return;}
    /* Other image codecs have not been integrated with this metadata API. */
    if(result && ((bytes>=8 && !memcmp(data,"\x89PNG\r\n\x1a\n",8)) || (bytes>=2 && ((data[0]=='B' && data[1]=='M') || (data[0]==0xff && data[1]==0xd8))))){recomp_unimpl("D3DX non-DDS image codec unsupported",src);return;}
    if(!result){unsigned char *destination=image_pointer(out,28,1);if(!destination)return;memcpy(destination,value,sizeof(value));}
    fnv_cpu_return_u32(result,12);
}
static int supported(float f){return isfinite(f) && (f==0 || fpclassify(f)!=FP_SUBNORMAL);}
static void matrix_multiply(int transpose){
    uint32_t out=fnv_cpu_argument_u32(0),left=fnv_cpu_argument_u32(1),right=fnv_cpu_argument_u32(2);
    unsigned char *destination=checked(out,1),*a=checked(left,0),*b=checked(right,0);
    if(!destination || !a || !b)return;
    if(fegetround()!=FE_TONEAREST){recomp_unimpl("D3DX matrix rounding mode unsupported",out);return;}
    float l[16],r[16],result[16];memcpy(l,a,64);memcpy(r,b,64);
    for(unsigned i=0;i<16;i++)if(!supported(l[i]) || !supported(r[i])){recomp_unimpl("D3DX matrix operand unsupported",out);return;}
    for(unsigned row=0;row<4;row++)for(unsigned col=0;col<4;col++){
        float products[4];for(unsigned k=0;k<4;k++){
            products[k]=l[row*4+k]*r[k*4+col];
            if(!supported(products[k])){recomp_unimpl("D3DX matrix product unsupported",out);return;}
        }
        /* Match the x86 SSE oracle's row-dependent reduction, without FMA.
         * A mathematically equivalent desktop-x64 reduction rounds differently. */
        float value=products[0]+products[1];
        if(!supported(value)){recomp_unimpl("D3DX matrix sum unsupported",out);return;}
        if(row==0 || row==3){float tail=products[2]+products[3];
            if(!supported(tail)){recomp_unimpl("D3DX matrix sum unsupported",out);return;}
            value=value+tail;
        }else {value=value+products[2];
            if(!supported(value)){recomp_unimpl("D3DX matrix sum unsupported",out);return;}
            value=value+products[3];}
        if(!supported(value)){recomp_unimpl("D3DX matrix sum unsupported",out);return;}
        result[transpose?col*4+row:row*4+col]=value;
    }
    memcpy(destination,result,64);fnv_cpu_return_u32(out,12);
}
static void api_matrix_multiply(void){matrix_multiply(0);}
static void api_matrix_multiply_transpose(void){matrix_multiply(1);}
static void api_matrix_rotation_yaw_pitch_roll(void){
    uint32_t out=fnv_cpu_argument_u32(0);float angle[3];
    unsigned char *destination=checked(out,1);if(!destination)return;
    for(unsigned i=0;i<3;i++){uint32_t bits=fnv_cpu_argument_u32(i+1);memcpy(angle+i,&bits,4);}
    if(fegetround()!=FE_TONEAREST){recomp_unimpl("D3DX rotation rounding mode unsupported",out);return;}
    for(unsigned i=0;i<3;i++)if(!supported(angle[i]) || fabsf(angle[i])>64.0f){
        recomp_unimpl("D3DX rotation angle unsupported",out);return;
    }
    /* Row vectors: roll around Z, pitch around X, then yaw around Y.
     * Native float libm is a bounded numerical implementation, not a claim of
     * bit identity with Microsoft's trigonometric approximation. No FMA. */
    float sy=sinf(angle[0]),cy=cosf(angle[0]),sp=sinf(angle[1]),cp=cosf(angle[1]);
    float sr=sinf(angle[2]),cr=cosf(angle[2]);
    float value[16]={cr*cy+sr*sp*sy,sr*cp,sr*sp*cy-cr*sy,0,
                    cr*sp*sy-sr*cy,cr*cp,sr*sy+cr*sp*cy,0,
                    cp*sy,-sp,cp*cy,0,0,0,0,1};
    for(unsigned i=0;i<16;i++)if(!supported(value[i])){recomp_unimpl("D3DX rotation result unsupported",out);return;}
    memcpy(destination,value,sizeof(value));fnv_cpu_return_u32(out,16);
}
static void api_perf_options(void){
    uint32_t options=fnv_cpu_argument_u32(0);
    if(options>1){recomp_unimpl("D3DPERF profiler options unsupported",options);return;}
    /* No external PIX profiler is attached to this native provider. Options 0
     * and 1 have no rendering side effect; 1 forbids external PIX profiling. */
    fnv_cpu_return_u32(0,4);
}
static const struct fnv_cpu_export exports[]={
    {"d3dx9_38.dll","D3DXMatrixMultiply",0,0,api_matrix_multiply},
    {"d3dx9_38.dll","D3DXMatrixMultiplyTranspose",0,0,api_matrix_multiply_transpose},
    {"d3dx9_38.dll","D3DXMatrixRotationYawPitchRoll",0,0,api_matrix_rotation_yaw_pitch_roll},
    {"d3dx9_38.dll","D3DXGetImageInfoFromFileInMemory",0,0,api_image_info_memory},
    {"d3d9.dll","D3DPERF_SetOptions",0,0,api_perf_options}};
const struct fnv_cpu_export *fnv_windows_d3dx_exports(size_t *count){if(count)*count=sizeof(exports)/sizeof(*exports);return exports;}
