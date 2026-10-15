#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include "d3d9_constants.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct fnv_d3d9_constants *bank;
static IDirect3DDevice9 *device;
static uint32_t shadow[2][3][1024],calls,fail_at,uploaded;
static unsigned comparisons,draws,shader_objects;
static int upload(void *context,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const uint32_t *bits) {
    (void)context;calls++;if(calls==fail_at)return 0;
    uint32_t width=k==2?1:4;memcpy(shadow[s][k]+first*width,bits,count*width*4);uploaded+=count*width*4;return 1;
}
static HRESULT native_set(unsigned s,unsigned k,unsigned first,unsigned count,const void *v) {
    if(!s){if(!k)return IDirect3DDevice9_SetVertexShaderConstantF(device,first,v,count);
        if(k==1)return IDirect3DDevice9_SetVertexShaderConstantI(device,first,v,count);
        return IDirect3DDevice9_SetVertexShaderConstantB(device,first,v,count);}
    if(!k)return IDirect3DDevice9_SetPixelShaderConstantF(device,first,v,count);
    if(k==1)return IDirect3DDevice9_SetPixelShaderConstantI(device,first,v,count);
    return IDirect3DDevice9_SetPixelShaderConstantB(device,first,v,count);
}
static HRESULT native_get(unsigned s,unsigned k,unsigned first,unsigned count,void *v) {
    if(!s){if(!k)return IDirect3DDevice9_GetVertexShaderConstantF(device,first,v,count);
        if(k==1)return IDirect3DDevice9_GetVertexShaderConstantI(device,first,v,count);
        return IDirect3DDevice9_GetVertexShaderConstantB(device,first,v,count);}
    if(!k)return IDirect3DDevice9_GetPixelShaderConstantF(device,first,v,count);
    if(k==1)return IDirect3DDevice9_GetPixelShaderConstantI(device,first,v,count);
    return IDirect3DDevice9_GetPixelShaderConstantB(device,first,v,count);
}
static void check_all(int check_shadow) {
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++) {
        unsigned count=k?16:s?224:256,width=k==2?1:4;uint32_t a[1024],b[1024];
        assert(!fnv_d3d9_constants_get(bank,s,k,0,count,a));assert(SUCCEEDED(native_get(s,k,0,count,b)));
        assert(!memcmp(a,b,count*width*4));comparisons++;
        if(check_shadow)assert(!memcmp(a,shadow[s][k],count*width*4));
    }
}
static uint32_t random_state=0x637291AF;
static uint32_t random32(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void contracts(void) {
    check_all(0);fail_at=2;assert(fnv_d3d9_constants_flush(bank,upload,NULL)==FNV_D3D9_UPLOAD_FAILED);
    assert(calls==2);fail_at=0;assert(!fnv_d3d9_constants_flush(bank,upload,NULL));assert(calls==7);check_all(1);
    calls=uploaded=0;assert(!fnv_d3d9_constants_flush(bank,upload,NULL) && !calls);
    for(unsigned n=0;n<600;n++) {
        unsigned s=random32()%2,k=random32()%3,limit=k?16:s?224:256;
        unsigned first=random32()%limit,count=1+random32()%(limit-first),width=k==2?1:4;
        uint32_t value[1024];for(unsigned i=0;i<count*width;i++)value[i]=random32();
        if(!k && count*width>=4){value[0]=0x7FC12345;value[1]=0x80000000;value[2]=0x7F800000;value[3]=0x00000001;}
        assert(SUCCEEDED(native_set(s,k,first,count,value)));
        assert(!fnv_d3d9_constants_set(bank,s,k,first,count,value));check_all(0);
        assert(!fnv_d3d9_constants_flush(bank,upload,NULL));check_all(1);
        unsigned previous=calls;assert(!fnv_d3d9_constants_set(bank,s,k,first,count,value));
        assert(!fnv_d3d9_constants_flush(bank,upload,NULL) && calls==previous);
    }
    uint32_t data[8]={1,2,3,4,5,6,7,8},before[8],after[8];
    assert(!fnv_d3d9_constants_get(bank,0,0,254,2,before));
    assert(fnv_d3d9_constants_set(bank,0,0,255,2,data)==FNV_D3D9_INVALIDCALL);
    assert(fnv_d3d9_constants_set(bank,0,0,0,UINT32_MAX,data)==FNV_D3D9_INVALIDCALL);
    assert(fnv_d3d9_constants_set(bank,2,0,0,1,data)==FNV_D3D9_INVALIDCALL);
    assert(!fnv_d3d9_constants_get(bank,0,0,254,2,after) && !memcmp(before,after,sizeof(before)));
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++) {
        unsigned limit=k?16:s?224:256;
        assert(SUCCEEDED(native_set(s,k,limit,0,NULL)));
        assert(SUCCEEDED(native_get(s,k,limit,0,NULL)));
        assert(!fnv_d3d9_constants_set(bank,s,k,limit,0,NULL));
        assert(!fnv_d3d9_constants_get(bank,s,k,limit,0,NULL));
    }
    calls=0;fail_at=2;
    assert(!fnv_d3d9_constants_set(bank,0,0,0,1,data));assert(SUCCEEDED(native_set(0,0,0,1,data)));
    assert(!fnv_d3d9_constants_set(bank,0,0,2,1,data+4));assert(SUCCEEDED(native_set(0,0,2,1,data+4)));
    assert(fnv_d3d9_constants_flush(bank,upload,NULL)==FNV_D3D9_UPLOAD_FAILED && calls==2);
    fail_at=0;assert(!fnv_d3d9_constants_flush(bank,upload,NULL) && calls==3);check_all(1);
    /* Two adjacent dirty registers straddle a dirty-bitset word. */
    calls=0;assert(!fnv_d3d9_constants_set(bank,0,0,63,2,data));
    assert(!fnv_d3d9_constants_flush(bank,upload,NULL) && calls==1);
    calls=0;fnv_d3d9_constants_invalidate(bank);assert(!fnv_d3d9_constants_flush(bank,upload,NULL) && calls==6);
}
static HANDLE start_event;
static DWORD WINAPI constants_worker(void *opaque) {
    unsigned stage=(unsigned)(uintptr_t)opaque;
    assert(WaitForSingleObject(start_event,INFINITE)==WAIT_OBJECT_0);
    for(unsigned i=0;i<500;i++) {
        uint32_t value[4]={stage,i,~i,0x80000000},got[4];
        assert(!fnv_d3d9_constants_set(bank,stage,0,20,1,value));
        assert(!fnv_d3d9_constants_get(bank,stage,0,20,1,got) && !memcmp(value,got,sizeof(got)));
        assert(!fnv_d3d9_constants_flush(bank,upload,NULL));
    }
    return 0;
}
static void concurrent(void) {
    start_event=CreateEventA(NULL,TRUE,FALSE,NULL);assert(start_event);
    HANDLE threads[2]={CreateThread(NULL,0,constants_worker,NULL,0,NULL),CreateThread(NULL,0,constants_worker,(void*)(uintptr_t)1,0,NULL)};
    assert(threads[0] && threads[1] && SetEvent(start_event));
    assert(WaitForMultipleObjects(2,threads,TRUE,INFINITE)==WAIT_OBJECT_0);
    for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    CloseHandle(start_event);
}
static uint32_t read_u32(FILE *f){unsigned char b[4];assert(fread(b,1,4,f)==4);return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void captured(const char *path) {
    fnv_d3d9_constants_destroy(bank);assert(!fnv_d3d9_constants_create(256,224,&bank));
    uint32_t zeros[1024]={0};for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++)assert(SUCCEEDED(native_set(s,k,0,k?16:s?224:256,zeros)));
    assert(!fnv_d3d9_constants_flush(bank,upload,NULL));calls=uploaded=0;
    FILE *f=fopen(path,"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9CST1",8));
    uint32_t events=read_u32(f);assert(events && events<10000);
    for(unsigned i=0;i<events;i++) {
        uint32_t type=read_u32(f),s=read_u32(f),first=read_u32(f),count=read_u32(f);
        if(!type){assert(s<2 && count<=256 && first<=256-count);uint32_t bits[1024];
            for(unsigned w=0;w<count*4;w++)bits[w]=read_u32(f);
            assert(!fnv_d3d9_constants_set(bank,s,0,first,count,bits));assert(SUCCEEDED(native_set(s,0,first,count,bits)));}
        else {assert(type==1 && !first && !count);assert(!fnv_d3d9_constants_flush(bank,upload,NULL));check_all(1);draws++;}
    }
    assert(fgetc(f)==EOF);fclose(f);
}
static void shaders(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9SHD1",8));
    uint32_t count=read_u32(f);assert(count && count<10000);
    for(unsigned i=0;i<count;i++) {
        uint32_t stage=read_u32(f),size=read_u32(f);assert(stage<2 && size>=8 && size<=1048576 && !(size&3));
        DWORD *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);UINT needed=0;
        if(!stage){IDirect3DVertexShader9 *s=NULL;assert(SUCCEEDED(IDirect3DDevice9_CreateVertexShader(device,bytes,&s)));
            assert(SUCCEEDED(IDirect3DVertexShader9_GetFunction(s,NULL,&needed)) && needed==size);
            void *copy=malloc(needed);assert(copy && SUCCEEDED(IDirect3DVertexShader9_GetFunction(s,copy,&needed)) && needed==size && !memcmp(bytes,copy,size));
            free(copy);IDirect3DVertexShader9_Release(s);}
        else {IDirect3DPixelShader9 *s=NULL;assert(SUCCEEDED(IDirect3DDevice9_CreatePixelShader(device,bytes,&s)));
            assert(SUCCEEDED(IDirect3DPixelShader9_GetFunction(s,NULL,&needed)) && needed==size);
            void *copy=malloc(needed);assert(copy && SUCCEEDED(IDirect3DPixelShader9_GetFunction(s,copy,&needed)) && needed==size && !memcmp(bytes,copy,size));
            free(copy);IDirect3DPixelShader9_Release(s);}
        shader_objects++;free(bytes);
    }
    assert(fgetc(f)==EOF);fclose(f);
}
int main(int argc,char **argv) {
    assert(argc==3);HWND window=CreateWindowExA(0,"STATIC","FNV contract oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    HRESULT h=IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
    assert(SUCCEEDED(h));assert(!fnv_d3d9_constants_create(256,224,&bank));contracts();captured(argv[1]);shaders(argv[2]);
    unsigned capture_calls=calls,capture_bytes=uploaded;concurrent();
    printf("{\"status\":\"PASS\",\"randomized_updates\":600,\"native_bank_comparisons\":%u,\"captured_draw_boundaries\":%u,\"capture_upload_runs\":%u,\"capture_upload_bytes\":%u,\"native_shader_roundtrips\":%u,\"concurrent_updates\":1000,\"native_engine_started\":false,\"rendered\":false}\n",comparisons,draws,capture_calls,capture_bytes,shader_objects);
    fnv_d3d9_constants_destroy(bank);IDirect3DDevice9_Release(device);IDirect3D9_Release(api);DestroyWindow(window);return 0;
}
