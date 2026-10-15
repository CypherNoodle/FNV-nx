#define main baseline_main
#include "d3d9_com_host.c"
#undef main
enum {CREATE_RT=20,CREATE_DS,GET_DESC,SET_RT,GET_RT,SET_DS,GET_DS,CLEAR,RESOURCE_TYPE,SET_VIEW,GET_VIEW};
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateRenderTarget)==28*sizeof(void*),"CreateRT slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateDepthStencilSurface)==29*sizeof(void*),"CreateDS slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetRenderTarget)==37*sizeof(void*),"SetRT slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,GetDepthStencilSurface)==40*sizeof(void*),"GetDS slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,Clear)==43*sizeof(void*),"Clear slot");
_Static_assert(offsetof(IDirect3DSurface9Vtbl,GetDesc)==12*sizeof(void*) && sizeof(D3DSURFACE_DESC)==32,"surface ABI");
static unsigned surface_creates,clear_calls,pixel_checks,failed_create_surface,failed_desc,failed_target;
static uintptr_t last_surface;
static ULONG last_device_release;
static void surface_device_release(void *ctx){(void)ctx;last_device_release=IDirect3DDevice9_Release(device);}
static uint32_t create_surface(void *ctx,int depth,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,uint32_t flag,uintptr_t *out) {
    (void)ctx;if(failed_create_surface)return 0x80004005;IDirect3DSurface9 *s=NULL;
    HRESULT r=depth?IDirect3DDevice9_CreateDepthStencilSurface(device,w,h,format,samples,quality,flag,&s,NULL):IDirect3DDevice9_CreateRenderTarget(device,w,h,format,samples,quality,flag,&s,NULL);
    if(SUCCEEDED(r)){*out=last_surface=(uintptr_t)s;surface_creates++;}return (uint32_t)r;
}
static uint32_t describe(void *ctx,uintptr_t handle,uint32_t out[8]) {
    (void)ctx;if(failed_desc)return 0x80004005;D3DSURFACE_DESC d;HRESULT h=IDirect3DSurface9_GetDesc((IDirect3DSurface9*)handle,&d);
    if(SUCCEEDED(h))memcpy(out,&d,32);return (uint32_t)h;
}
static uint32_t set_target(void *ctx,int depth,uint32_t index,uintptr_t handle) {
    (void)ctx;if(failed_target)return 0x80004005;
    return (uint32_t)(depth?IDirect3DDevice9_SetDepthStencilSurface(device,(IDirect3DSurface9*)handle):IDirect3DDevice9_SetRenderTarget(device,index,(IDirect3DSurface9*)handle));
}
static uint32_t get_target(void *ctx,int depth,uint32_t index,uintptr_t *out) {
    (void)ctx;if(failed_target)return 0x80004005;IDirect3DSurface9 *s=NULL;
    HRESULT h=depth?IDirect3DDevice9_GetDepthStencilSurface(device,&s):IDirect3DDevice9_GetRenderTarget(device,index,&s);
    if(SUCCEEDED(h))*out=(uintptr_t)s;return (uint32_t)h;
}
static uint32_t clear_target(void *ctx,uint32_t count,const uint32_t *rects,uint32_t flags,uint32_t color,uint32_t bits,uint32_t stencil) {
    (void)ctx;float depth;memcpy(&depth,&bits,4);clear_calls++;
    return (uint32_t)IDirect3DDevice9_Clear(device,count,(const D3DRECT*)rects,flags,color,depth,stencil);
}
static uint32_t state(void *ctx,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get_state) {
    (void)ctx;(void)sampler;(void)name;assert(kind==FNV_D3D9_VIEWPORT);D3DVIEWPORT9 v;HRESULT h;
    if(!get_state)memcpy(&v,values,24);h=get_state?IDirect3DDevice9_GetViewport(device,&v):IDirect3DDevice9_SetViewport(device,&v);
    if(get_state && SUCCEEDED(h))memcpy(values,&v,24);return (uint32_t)h;
}
static HWND setup_target(const char *fixture,int provider) {
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(fixture,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config cfg={0};cfg.memory=arena;cfg.guest_base=BASE;cfg.memory_bytes=ARENA;cfg.code_low=BASE+0x1000;cfg.code_high=BASE+0x2000;assert(fnv_cpu_create(&cfg,&cpu)==FNV_CPU_OK);registry_checks();
    HWND w=CreateWindowExA(0,"STATIC","FNV target HAL oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;
    assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,.device_release=surface_device_release,
            .create_surface=provider?create_surface:NULL,.surface_desc=describe,.set_target=set_target,.get_target=get_target,.clear=clear_target,.state=state}};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);return w;
}
static uint32_t make_surface(int depth,uint32_t size) {
    uint32_t args[]={device_at,size,size,depth?D3DFMT_D24S8:D3DFMT_A8R8G8B8,0,0,0,GUEST_OUT,0};assert(!call(depth?CREATE_DS:CREATE_RT,9,args));return get(GUEST_OUT);
}
static void description(uint32_t surface,uintptr_t native) {
    uint32_t args[]={surface,DATA};assert(!call(GET_DESC,2,args));D3DSURFACE_DESC d;assert(SUCCEEDED(IDirect3DSurface9_GetDesc((IDirect3DSurface9*)native,&d)) && !memcmp(&d,ptr(DATA),32));
    assert(call(RESOURCE_TYPE,1,&surface)==D3DRTYPE_SURFACE);
    const GUID *ids[]={&IID_IDirect3DSurface9,&IID_IDirect3DResource9};
    for(unsigned i=0;i<2;i++){memcpy(ptr(DATA),ids[i],16);uint32_t qi[]={surface,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==surface);release(surface);}
    uint32_t parent[]={surface,GUEST_OUT};assert(!call(GET_DEVICE,2,parent) && get(GUEST_OUT)==device_at);release(device_at);
}
static void check_pixels(IDirect3DSurface9 *surface,uint32_t color,uint32_t patch) {
    D3DSURFACE_DESC d;assert(SUCCEEDED(IDirect3DSurface9_GetDesc(surface,&d)));IDirect3DSurface9 *readback=NULL;
    assert(SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&readback,NULL)));
    assert(SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(device,surface,readback)));D3DLOCKED_RECT lock;
    assert(SUCCEEDED(IDirect3DSurface9_LockRect(readback,&lock,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<d.Height;y++)for(unsigned x=0;x<d.Width;x++){
        DWORD expected=patch && x>=2 && x<6 && y>=3 && y<7?patch:color;
        DWORD actual;memcpy(&actual,(char*)lock.pBits+y*lock.Pitch+x*4,4);assert(actual==expected);pixel_checks++;
    }
    assert(SUCCEEDED(IDirect3DSurface9_UnlockRect(readback)));IDirect3DSurface9_Release(readback);
}
int main(int argc,char **argv) {
    assert(argc==4);if(strcmp(argv[3],"normal") && strncmp(argv[3],"target_",7))return baseline_main(argc,argv);
    HWND w=setup_target(argv[1],strcmp(argv[3],"target_provider")!=0);
    if(!strcmp(argv[3],"target_provider")){uint32_t args[]={device_at,32,32,D3DFMT_A8R8G8B8,0,0,0,GUEST_OUT,0};struct fnv_cpu_fault fault;
        assert(invoke(CREATE_RT,9,args,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);}
    else if(!strcmp(argv[3],"target_parent")){uint32_t rt=make_surface(0,32),ds=make_surface(1,32),set[]={device_at,0,rt};assert(!call(SET_RT,3,set));uint32_t depth[]={device_at,ds};assert(!call(SET_DS,2,depth));assert(!release(rt) && !release(ds) && !release(device_at));}
    else {
        uint32_t held[32];for(unsigned i=0;i<32;i++)held[i]=make_surface(0,16);
        uint32_t get_rt[]={device_at,0,GUEST_OUT};assert(call(GET_RT,3,get_rt)==FNV_D3D9_OUTOFMEMORY && !get(GUEST_OUT));
        uint32_t overflow[]={device_at,16,16,D3DFMT_A8R8G8B8,0,0,0,GUEST_OUT,0};assert(call(CREATE_RT,9,overflow)==FNV_D3D9_OUTOFMEMORY && !get(GUEST_OUT));
        for(unsigned i=0;i<32;i++)assert(!release(held[i]));
        assert(!call(GET_RT,3,get_rt));uint32_t back=get(GUEST_OUT);IDirect3DSurface9 *native_back=NULL;
        assert(SUCCEEDED(IDirect3DDevice9_GetRenderTarget(device,0,&native_back)));description(back,(uintptr_t)native_back);
        assert(!call(GET_RT,3,get_rt) && get(GUEST_OUT)==back);assert(release(back)==1);
        uint32_t ds_args[]={device_at,GUEST_OUT};assert(call(GET_DS,2,ds_args)==(uint32_t)D3DERR_NOTFOUND && !get(GUEST_OUT));
        uint32_t rt=make_surface(0,32);uintptr_t native_rt=last_surface;description(rt,native_rt);uint32_t ds=make_surface(1,32);description(ds,last_surface);
        D3DVIEWPORT9 small={1,1,8,8,0,1};memcpy(ptr(DATA),&small,24);uint32_t vp[]={device_at,DATA};assert(!call(SET_VIEW,2,vp));
        uint32_t set[]={device_at,0,rt};assert(!call(SET_RT,3,set));assert(!call(GET_VIEW,2,vp));D3DVIEWPORT9 actual;memcpy(&actual,ptr(DATA),24);assert(!actual.X && !actual.Y && actual.Width==32 && actual.Height==32);
        uint32_t depth[]={device_at,ds};assert(!call(SET_DS,2,depth));assert(!release(rt) && !release(ds));
        assert(!call(GET_RT,3,get_rt) && get(GUEST_OUT)==rt);assert(!release(rt));assert(!call(GET_DS,2,ds_args) && get(GUEST_OUT)==ds);assert(!release(ds));
        uint32_t clear[]={device_at,0,0,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xFF13579B,0x3F000000,7};assert(!call(CLEAR,7,clear));check_pixels((IDirect3DSurface9*)native_rt,clear[4],0);
        D3DRECT rect={2,3,6,7};memcpy(ptr(DATA),&rect,16);clear[1]=1;clear[2]=DATA;clear[3]=D3DCLEAR_TARGET;clear[4]=0xFFABCDEF;assert(!call(CLEAR,7,clear));check_pixels((IDirect3DSurface9*)native_rt,0xFF13579B,clear[4]);
        clear[2]=BASE+ARENA-4;unsigned before=clear_calls;assert(call(CLEAR,7,clear)==FNV_D3D9_INVALIDCALL && clear_calls==before);
        failed_target=1;set[2]=back;assert(call(SET_RT,3,set)==0x80004005);failed_target=0;assert(!call(GET_RT,3,get_rt) && get(GUEST_OUT)==rt);assert(!release(rt));
        set[2]=0;assert(call(SET_RT,3,set)==FNV_D3D9_INVALIDCALL);depth[1]=0;assert(!call(SET_DS,2,depth));set[2]=back;assert(!call(SET_RT,3,set));assert(!release(back));IDirect3DSurface9_Release(native_back);
        for(unsigned i=0;i<128;i++){uint32_t s=make_surface(i&1,16);assert(!release(s));}
        uint32_t args[]={device_at,16,16,D3DFMT_A8R8G8B8,0,0,0,GUEST_OUT,0};failed_create_surface=1;assert(call(CREATE_RT,9,args)==0x80004005 && !get(GUEST_OUT));failed_create_surface=0;
        failed_desc=1;assert(call(CREATE_RT,9,args)==0x80004005 && !get(GUEST_OUT));failed_desc=0;
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK && !last_device_release);assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(w);
    printf("{\"status\":\"PASS\",\"prepared_methods\":63,\"surface_creates\":%u,\"clear_calls\":%u,\"pixel_checks\":%u,\"desktop_hal_clear_verified\":%s,\"gpu_rendered\":%s,\"gpu_draw_executed\":false,\"game_frame_rendered\":false}\n",surface_creates,clear_calls,pixel_checks,clear_calls==2?"true":"false",clear_calls?"true":"false");return 0;
}
