#define main baseline_main
#include "d3d9_com_host.c"
#undef main
enum {SET_RENDER=20,GET_RENDER,SET_SAMPLER,GET_SAMPLER,SET_VIEW,GET_VIEW,SET_SCISSOR,GET_SCISSOR};
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetRenderState)==57*sizeof(void*),"render slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetSamplerState)==69*sizeof(void*),"sampler slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetViewport)==47*sizeof(void*),"viewport slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetScissorRect)==75*sizeof(void*),"scissor slot");
_Static_assert(sizeof(D3DVIEWPORT9)==24 && sizeof(RECT)==16,"state struct ABI");
static unsigned provider_calls,scalar_checks,struct_checks,failed_provider;
static uint32_t backend_state(void *ctx,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get_state) {
    (void)ctx;provider_calls++;
    if(failed_provider){memset(values,0xDE,24);return 0x80004005;}
    switch(kind) {
        case FNV_D3D9_RENDER_STATE:return (uint32_t)(get_state?IDirect3DDevice9_GetRenderState(device,name,(DWORD*)values):IDirect3DDevice9_SetRenderState(device,name,values[0]));
        case FNV_D3D9_SAMPLER_STATE:return (uint32_t)(get_state?IDirect3DDevice9_GetSamplerState(device,sampler,name,(DWORD*)values):IDirect3DDevice9_SetSamplerState(device,sampler,name,values[0]));
        case FNV_D3D9_VIEWPORT:{D3DVIEWPORT9 view;HRESULT h;if(!get_state)memcpy(&view,values,sizeof(view));
            h=get_state?IDirect3DDevice9_GetViewport(device,&view):IDirect3DDevice9_SetViewport(device,&view);
            if(SUCCEEDED(h) && get_state)memcpy(values,&view,sizeof(view));return (uint32_t)h;}
        case FNV_D3D9_SCISSOR:{RECT rect;HRESULT h;if(!get_state)memcpy(&rect,values,sizeof(rect));
            h=get_state?IDirect3DDevice9_GetScissorRect(device,&rect):IDirect3DDevice9_SetScissorRect(device,&rect);
            if(SUCCEEDED(h) && get_state)memcpy(values,&rect,sizeof(rect));return (uint32_t)h;}
        default:return FNV_D3D9_INVALIDCALL;
    }
}
static HWND setup_state(int provider) {
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_cpu_config cfg={0};cfg.memory=arena;cfg.guest_base=BASE;cfg.memory_bytes=ARENA;cfg.code_low=BASE+0x1000;cfg.code_high=BASE+0x2000;
    assert(fnv_cpu_create(&cfg,&cpu)==FNV_CPU_OK);registry_checks();
    HWND window=CreateWindowExA(0,"STATIC","FNV state oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,.device_release=device_release,.state=provider?backend_state:NULL}};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);return window;
}
static void scalar(unsigned sampler,uint32_t slot,uint32_t name,uint32_t value) {
    DWORD expected=0;HRESULT h;
    if(sampler){assert(SUCCEEDED(IDirect3DDevice9_GetSamplerState(device,slot,name,&expected)));uint32_t initial[]={device_at,slot,name,GUEST_OUT};assert(!call(GET_SAMPLER,4,initial) && get(GUEST_OUT)==expected);
        h=IDirect3DDevice9_SetSamplerState(device,slot,name,value);if(SUCCEEDED(h))assert(SUCCEEDED(IDirect3DDevice9_GetSamplerState(device,slot,name,&expected)));
        uint32_t args[]={device_at,slot,name,value};assert(call(SET_SAMPLER,4,args)==(uint32_t)h);args[3]=GUEST_OUT;assert(!call(GET_SAMPLER,4,args) && get(GUEST_OUT)==expected);}
    else {assert(SUCCEEDED(IDirect3DDevice9_GetRenderState(device,name,&expected)));uint32_t initial[]={device_at,name,GUEST_OUT};assert(!call(GET_RENDER,3,initial) && get(GUEST_OUT)==expected);
        h=IDirect3DDevice9_SetRenderState(device,name,value);if(SUCCEEDED(h))assert(SUCCEEDED(IDirect3DDevice9_GetRenderState(device,name,&expected)));
        uint32_t args[]={device_at,name,value};assert(call(SET_RENDER,3,args)==(uint32_t)h);args[2]=GUEST_OUT;assert(!call(GET_RENDER,3,args) && get(GUEST_OUT)==expected);}
    scalar_checks++;
}
static void spatial(void) {
    D3DVIEWPORT9 view;RECT rect;assert(SUCCEEDED(IDirect3DDevice9_GetViewport(device,&view)));
    uint32_t args[]={device_at,DATA};assert(!call(GET_VIEW,2,args) && !memcmp(ptr(DATA),&view,24));
    assert(SUCCEEDED(IDirect3DDevice9_GetScissorRect(device,&rect)));assert(!call(GET_SCISSOR,2,args) && !memcmp(ptr(DATA),&rect,16));
    for(unsigned i=0;i<64;i++) {
        view=(D3DVIEWPORT9){i%4,i%3,8+i%20,8+i%20,0.0f,1.0f};memcpy(ptr(DATA),&view,24);assert(!call(SET_VIEW,2,args));
        D3DVIEWPORT9 actual;assert(SUCCEEDED(IDirect3DDevice9_GetViewport(device,&actual)) && !memcmp(&actual,&view,24));memset(ptr(DATA),0,24);assert(!call(GET_VIEW,2,args) && !memcmp(ptr(DATA),&view,24));
        rect=(RECT){(LONG)(i%3),(LONG)(i%4),(LONG)(8+i%20),(LONG)(8+i%20)};memcpy(ptr(DATA),&rect,16);assert(!call(SET_SCISSOR,2,args));
        RECT actual_rect;assert(SUCCEEDED(IDirect3DDevice9_GetScissorRect(device,&actual_rect)) && !memcmp(&actual_rect,&rect,16));memset(ptr(DATA),0,16);assert(!call(GET_SCISSOR,2,args) && !memcmp(ptr(DATA),&rect,16));struct_checks+=2;
    }
    /* Provider may mutate its temporary output on failure: guest stays intact. */
    memset(ptr(DATA),0xA7,24);failed_provider=1;assert(call(GET_VIEW,2,args)==0x80004005);
    for(unsigned i=0;i<24;i++)assert(ptr(DATA)[i]==0xA7);failed_provider=0;
    unsigned calls_before=provider_calls;args[1]=BASE+ARENA-8;assert(call(SET_VIEW,2,args)==FNV_D3D9_INVALIDCALL && call(GET_SCISSOR,2,args)==FNV_D3D9_INVALIDCALL && provider_calls==calls_before);
}
int main(int argc,char **argv) {
    assert(argc==4);if(strcmp(argv[3],"normal") && strcmp(argv[3],"state_provider"))return baseline_main(argc,argv);
    /* Load only the synthetic caller fixture into the own flat arena. */
    HWND window=setup_state(strcmp(argv[3],"state_provider")!=0);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    if(!strcmp(argv[3],"state_provider")){uint32_t args[]={device_at,D3DRS_ZENABLE,1};struct fnv_cpu_fault fault;assert(invoke(SET_RENDER,3,args,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);}
    else {
        const struct {D3DRENDERSTATETYPE name;DWORD value;} render[]={
            {D3DRS_ZENABLE,D3DZB_TRUE},{D3DRS_ZWRITEENABLE,0},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},
            {D3DRS_ALPHATESTENABLE,1},{D3DRS_ALPHAREF,127},{D3DRS_ALPHAFUNC,D3DCMP_GREATER},
            {D3DRS_ALPHABLENDENABLE,1},{D3DRS_SRCBLEND,D3DBLEND_SRCALPHA},{D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA},
            {D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,1},{D3DRS_SRCBLENDALPHA,D3DBLEND_ONE},{D3DRS_DESTBLENDALPHA,D3DBLEND_ZERO},
            {D3DRS_CULLMODE,D3DCULL_CCW},{D3DRS_STENCILENABLE,1},{D3DRS_STENCILFUNC,D3DCMP_EQUAL},{D3DRS_STENCILREF,128},
            {D3DRS_STENCILMASK,0xAA55AA55},{D3DRS_STENCILWRITEMASK,0x55AA55AA},{D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE},
            {D3DRS_TWOSIDEDSTENCILMODE,1},{D3DRS_CCW_STENCILFUNC,D3DCMP_ALWAYS},{D3DRS_COLORWRITEENABLE,7},
            {D3DRS_SCISSORTESTENABLE,1},{D3DRS_SRGBWRITEENABLE,1},{D3DRS_MULTISAMPLEMASK,0x87654321},
            {D3DRS_DEPTHBIAS,0x80000000},{D3DRS_SLOPESCALEDEPTHBIAS,0x7FC12345},{D3DRS_BLENDFACTOR,0xAABBCCDD}};
        for(unsigned i=0;i<sizeof(render)/sizeof(*render);i++)scalar(0,0,render[i].name,render[i].value);
        const struct {D3DSAMPLERSTATETYPE name;DWORD value;} samplers[]={
            {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP},{D3DSAMP_ADDRESSW,D3DTADDRESS_MIRROR},
            {D3DSAMP_BORDERCOLOR,0xAB123456},{D3DSAMP_MAGFILTER,D3DTEXF_LINEAR},{D3DSAMP_MINFILTER,D3DTEXF_POINT},
            {D3DSAMP_MIPFILTER,D3DTEXF_LINEAR},{D3DSAMP_MIPMAPLODBIAS,0xBF800000},{D3DSAMP_MAXMIPLEVEL,2},{D3DSAMP_MAXANISOTROPY,4},{D3DSAMP_SRGBTEXTURE,1}};
        for(unsigned slot=0;slot<16;slot++)for(unsigned i=0;i<sizeof(samplers)/sizeof(*samplers);i++)scalar(1,slot,samplers[i].name,samplers[i].value);
        for(unsigned i=0;i<256;i++)scalar(0,0,D3DRS_BLENDFACTOR,0x10203040u*i);
        spatial();DWORD prior=0;assert(SUCCEEDED(IDirect3DDevice9_GetRenderState(device,D3DRS_ZENABLE,&prior)));
        failed_provider=1;uint32_t set[]={device_at,D3DRS_ZENABLE,0};assert(call(SET_RENDER,3,set)==0x80004005);failed_provider=0;
        DWORD after=0;assert(SUCCEEDED(IDirect3DDevice9_GetRenderState(device,D3DRS_ZENABLE,&after)) && after==prior);
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    printf("{\"status\":\"PASS\",\"prepared_methods\":55,\"scalar_state_checks\":%u,\"spatial_checks\":%u,\"provider_calls\":%u}\n",scalar_checks,struct_checks,provider_calls);return 0;
}
