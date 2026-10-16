/* Own tiny uncompressed texture provider for the COM/Vulkan integration test.
 * NULLREF is the reference/lifetime oracle, not the pixel renderer. */
#include "d3d9_texture_upload.h"
enum {T_CREATE=32,T_LOCK,T_UNLOCK,T_BIND,T_GET};
static uintptr_t texture_handle;
static uint32_t texture_resource;
static unsigned char texture_current[16],texture_saved[3][16],texture_expected[3][16];
static unsigned texture_recorded,texture_uploads,texture_failed_uploads;
static int texture_bound,texture_fail_upload,texture_fail_bind;
static uint32_t texture_create(void *ctx,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    (void)ctx;if(w!=2 || h!=2 || levels!=1 || usage || format!=21 || pool!=D3DPOOL_MANAGED || texture_handle)return FNV_D3D9_INVALIDCALL;
    HRESULT hr=IDirect3DDevice9_CreateTexture(device,w,h,levels,usage,format,pool,(IDirect3DTexture9**)out,NULL);
    if(SUCCEEDED(hr))texture_handle=*out;
    return (uint32_t)hr;
}
static uint32_t texture_upload(void *ctx,uintptr_t handle,uint32_t level,uint32_t left,uint32_t top,uint32_t width,uint32_t height,uint32_t flags,uint32_t pitch,const void *data){
    (void)ctx;if(handle!=texture_handle || level || flags)return FNV_D3D9_INVALIDCALL;
    if(texture_fail_upload){texture_fail_upload=0;texture_failed_uploads++;return 0x80004005;}
    unsigned char next[16];memcpy(next,texture_current,16);
    struct fnv_texture_patch patch={2,2,left,top,width,height,21,pitch,data,(size_t)(height-1)*pitch+width*4};
    uint32_t result=fnv_d3d9_texture_rgba8(&patch,next,16,8);if(result)return result;
    RECT rect={(LONG)left,(LONG)top,(LONG)(left+width),(LONG)(top+height)};D3DLOCKED_RECT mapped;
    HRESULT hr=IDirect3DTexture9_LockRect((IDirect3DTexture9*)handle,0,&mapped,&rect,0);if(FAILED(hr))return (uint32_t)hr;
    for(unsigned y=0;y<height;++y)memcpy((unsigned char*)mapped.pBits+(size_t)y*mapped.Pitch,(const unsigned char*)data+(size_t)y*pitch,width*4);
    hr=IDirect3DTexture9_UnlockRect((IDirect3DTexture9*)handle,0);
    if(SUCCEEDED(hr)){memcpy(texture_current,next,16);texture_uploads++;}
    return (uint32_t)hr;
}
static uint32_t texture_bind(void *ctx,uint32_t sampler,uintptr_t handle){
    (void)ctx;if(sampler || (handle && handle!=texture_handle))return FNV_D3D9_INVALIDCALL;
    if(texture_fail_bind){texture_fail_bind=0;return 0x80004005;}
    HRESULT hr=IDirect3DDevice9_SetTexture(device,sampler,(IDirect3DBaseTexture9*)handle);
    if(SUCCEEDED(hr))texture_bound=handle!=0;
    return (uint32_t)hr;
}
static void texture_setup(void){
    uint32_t args[]={device_at,2,2,1,0,21,D3DPOOL_MANAGED,GUEST_OUT,0};assert(!call(T_CREATE,9,args));texture_resource=get(GUEST_OUT);
    uint32_t bind[]={device_at,0,texture_resource};assert(!call(T_BIND,3,bind));
}
static void texture_frame(unsigned frame){
    uint32_t args[]={texture_resource,0,GUEST_OUT,0,0};assert(!call(T_LOCK,5,args));
    uint32_t pitch=get(GUEST_OUT),address=get(GUEST_OUT+4);assert(pitch>=8);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){
        unsigned pixel=y*2+x;unsigned char *d=ptr(address)+y*pitch+x*4;
        d[0]=(unsigned char)(31+frame*20+pixel*11);d[1]=(unsigned char)(47+frame*17+pixel*13);
        d[2]=(unsigned char)(173-frame*19-pixel*23);d[3]=(unsigned char)(127+pixel*29);
        unsigned char *e=texture_expected[frame]+pixel*4;e[0]=d[2];e[1]=d[1];e[2]=d[0];e[3]=d[3];
    }
    uint32_t unlock[]={texture_resource,0};assert(!call(T_UNLOCK,2,unlock));
    /* Last pixel partial patch must preserve all other rows and channels. */
    RECT rect={1,1,2,2};memcpy(ptr(DATA),&rect,sizeof(rect));args[3]=DATA;assert(!call(T_LOCK,5,args));address=get(GUEST_OUT+4);
    const unsigned char bgr[4]={(unsigned char)(201-frame*21),(unsigned char)(19+frame*31),(unsigned char)(67+frame*17),93};
    memcpy(ptr(address),bgr,4);unsigned char old[16];memcpy(old,texture_current,16);
    texture_fail_upload=1;assert(call(T_UNLOCK,2,unlock)==0x80004005 && !memcmp(old,texture_current,16));
    assert(!call(T_UNLOCK,2,unlock));
    unsigned char *e=texture_expected[frame]+12;e[0]=bgr[2];e[1]=bgr[1];e[2]=bgr[0];e[3]=bgr[3];
    assert(!memcmp(texture_current,texture_expected[frame],16));
}
static void texture_record(void){
    assert(texture_bound && texture_recorded<3);
    memcpy(texture_saved[texture_recorded],texture_current,16);texture_recorded++;
}
static void texture_poison(void){
    uint32_t args[]={texture_resource,0,GUEST_OUT,0,0};assert(!call(T_LOCK,5,args));
    uint32_t pitch=get(GUEST_OUT),address=get(GUEST_OUT+4);for(unsigned y=0;y<2;++y)memset(ptr(address)+y*pitch,0,8);
    assert(!call(T_UNLOCK,2,args)); /* this,level are the first two arguments */
    const unsigned char zero[16]={0};assert(!memcmp(texture_current,zero,16));
    for(unsigned i=0;i<3;++i)assert(!memcmp(texture_saved[i],texture_expected[i],16) && memcmp(texture_saved[i],texture_current,16));
    uint32_t bind[]={device_at,0,0};texture_fail_bind=1;assert(call(T_BIND,3,bind)==0x80004005 && texture_bound);
    uint32_t get_args[]={device_at,0,GUEST_OUT};assert(!call(T_GET,3,get_args) && get(GUEST_OUT)==texture_resource);assert(release(texture_resource)==1);
}
static void texture_cleanup(void){
    uint32_t args[]={device_at,0,0};assert(!call(T_BIND,3,args));assert(!release(texture_resource));
}
