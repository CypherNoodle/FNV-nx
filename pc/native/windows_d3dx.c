/* Own bounded D3DX math bridge; no implementation from a Windows DLL. */
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
static int supported(float f){return isfinite(f) && (f==0 || fpclassify(f)!=FP_SUBNORMAL);}
static void api_matrix_multiply(void){
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
        result[row*4+col]=value;
    }
    memcpy(destination,result,64);fnv_cpu_return_u32(out,12);
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
    {"d3d9.dll","D3DPERF_SetOptions",0,0,api_perf_options}};
const struct fnv_cpu_export *fnv_windows_d3dx_exports(size_t *count){if(count)*count=sizeof(exports)/sizeof(*exports);return exports;}
