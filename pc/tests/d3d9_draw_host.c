#define main baseline_main
#include "d3d9_com_host.c"
#undef main
#include <d3dcompiler.h>
enum {BEGIN=20,END,DRAW,DRAW_INDEXED,CREATE_VB,CREATE_IB,LOCK,UNLOCK,STREAM,INDICES,CREATE_DECL,SET_DECL,
    CREATE_TEX,LOCK_TEX,UNLOCK_TEX,SET_TEX,CREATE_RT,SET_RT,CREATE_DS,SET_DS,CLEAR,RENDER,SAMPLER,SET_PS,GET_STREAM,GET_INDICES};
_Static_assert(offsetof(IDirect3DDevice9Vtbl,DrawPrimitive)==81*sizeof(void*),"Draw slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,DrawIndexedPrimitive)==82*sizeof(void*),"DIP slot");
static unsigned draws,constant_uploads,pixels,failed_constants,failed_draw,failed_scene;
static uintptr_t target;
static ULONG final_device_refs;
static ID3DBlob *shader_blobs[2];
static HMODULE compiler;
static void drop_device(void *ctx){(void)ctx;final_device_refs=IDirect3DDevice9_Release(device);}
static uint32_t bind_shader(void *ctx,enum fnv_d3d9_stage stage,uintptr_t handle){(void)ctx;return (uint32_t)(stage?IDirect3DDevice9_SetPixelShader(device,(IDirect3DPixelShader9*)handle):IDirect3DDevice9_SetVertexShader(device,(IDirect3DVertexShader9*)handle));}
static uint32_t create_buffer(void *ctx,enum fnv_d3d9_resource_kind kind,uint32_t bytes,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    (void)ctx;void *p=NULL;HRESULT h=kind==FNV_D3D9_VB?IDirect3DDevice9_CreateVertexBuffer(device,bytes,usage,format,pool,(IDirect3DVertexBuffer9**)&p,NULL):IDirect3DDevice9_CreateIndexBuffer(device,bytes,usage,format,pool,(IDirect3DIndexBuffer9**)&p,NULL);if(SUCCEEDED(h))*out=(uintptr_t)p;return (uint32_t)h;}
static uint32_t buffer_data(void *ctx,uintptr_t handle,uint32_t offset,uint32_t bytes,uint32_t flags,const void *data){
    (void)ctx;IDirect3DResource9 *r=(IDirect3DResource9*)handle;int vb=IDirect3DResource9_GetType(r)==D3DRTYPE_VERTEXBUFFER;void *p=NULL;
    HRESULT h=vb?IDirect3DVertexBuffer9_Lock((IDirect3DVertexBuffer9*)handle,offset,bytes,&p,flags):IDirect3DIndexBuffer9_Lock((IDirect3DIndexBuffer9*)handle,offset,bytes,&p,flags);
    if(FAILED(h))return (uint32_t)h;memcpy(p,data,bytes);return (uint32_t)(vb?IDirect3DVertexBuffer9_Unlock((IDirect3DVertexBuffer9*)handle):IDirect3DIndexBuffer9_Unlock((IDirect3DIndexBuffer9*)handle));}
static uint32_t stream(void *ctx,uint32_t slot,uintptr_t handle,uint32_t offset,uint32_t stride){(void)ctx;return (uint32_t)IDirect3DDevice9_SetStreamSource(device,slot,(IDirect3DVertexBuffer9*)handle,offset,stride);}
static uint32_t indices(void *ctx,uintptr_t handle){(void)ctx;return (uint32_t)IDirect3DDevice9_SetIndices(device,(IDirect3DIndexBuffer9*)handle);}
static uint32_t create_decl(void *ctx,const void *data,uint32_t count,uintptr_t *out){(void)ctx;(void)count;IDirect3DVertexDeclaration9 *d=NULL;HRESULT h=IDirect3DDevice9_CreateVertexDeclaration(device,data,&d);if(SUCCEEDED(h))*out=(uintptr_t)d;return (uint32_t)h;}
static uint32_t bind_decl(void *ctx,uintptr_t handle){(void)ctx;return (uint32_t)IDirect3DDevice9_SetVertexDeclaration(device,(IDirect3DVertexDeclaration9*)handle);}
static uint32_t create_tex(void *ctx,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){(void)ctx;IDirect3DTexture9 *t=NULL;HRESULT r=IDirect3DDevice9_CreateTexture(device,w,h,levels,usage,format,pool,&t,NULL);if(SUCCEEDED(r))*out=(uintptr_t)t;return (uint32_t)r;}
static uint32_t texture_data(void *ctx,uintptr_t handle,uint32_t level,uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint32_t flags,uint32_t pitch,const void *data){
    (void)ctx;RECT rect={(LONG)x,(LONG)y,(LONG)(x+w),(LONG)(y+h)};D3DLOCKED_RECT lock;HRESULT r=IDirect3DTexture9_LockRect((IDirect3DTexture9*)handle,level,&lock,&rect,flags);
    if(FAILED(r))return (uint32_t)r;for(unsigned row=0;row<h;row++)memcpy((char*)lock.pBits+row*lock.Pitch,(const char*)data+row*pitch,w*4);
    return (uint32_t)IDirect3DTexture9_UnlockRect((IDirect3DTexture9*)handle,level);}
static uint32_t bind_tex(void *ctx,uint32_t sampler,uintptr_t handle){(void)ctx;return (uint32_t)IDirect3DDevice9_SetTexture(device,sampler,(IDirect3DBaseTexture9*)handle);}
static uint32_t surface(void *ctx,int depth,uint32_t w,uint32_t h,uint32_t fmt,uint32_t samples,uint32_t quality,uint32_t flag,uintptr_t *out){
    (void)ctx;IDirect3DSurface9 *s=NULL;HRESULT r=depth?IDirect3DDevice9_CreateDepthStencilSurface(device,w,h,fmt,samples,quality,flag,&s,NULL):IDirect3DDevice9_CreateRenderTarget(device,w,h,fmt,samples,quality,flag,&s,NULL);
    if(SUCCEEDED(r)){*out=(uintptr_t)s;if(!depth)target=*out;}return (uint32_t)r;}
static uint32_t desc(void *ctx,uintptr_t handle,uint32_t out[8]){(void)ctx;D3DSURFACE_DESC d;HRESULT r=IDirect3DSurface9_GetDesc((IDirect3DSurface9*)handle,&d);if(SUCCEEDED(r))memcpy(out,&d,32);return (uint32_t)r;}
static uint32_t bind_target(void *ctx,int depth,uint32_t slot,uintptr_t handle){(void)ctx;return (uint32_t)(depth?IDirect3DDevice9_SetDepthStencilSurface(device,(IDirect3DSurface9*)handle):IDirect3DDevice9_SetRenderTarget(device,slot,(IDirect3DSurface9*)handle));}
static uint32_t clear(void *ctx,uint32_t count,const uint32_t *rects,uint32_t flags,uint32_t color,uint32_t bits,uint32_t stencil){(void)ctx;float z;memcpy(&z,&bits,4);return (uint32_t)IDirect3DDevice9_Clear(device,count,(const D3DRECT*)rects,flags,color,z,stencil);}
static uint32_t state(void *ctx,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get_state){
    (void)ctx;assert(!get_state);if(kind==FNV_D3D9_RENDER_STATE)return (uint32_t)IDirect3DDevice9_SetRenderState(device,name,values[0]);assert(kind==FNV_D3D9_SAMPLER_STATE);return (uint32_t)IDirect3DDevice9_SetSamplerState(device,sampler,name,values[0]);}
static uint32_t scene(void *ctx,int begin){(void)ctx;if(failed_scene)return 0x80004005;return (uint32_t)(begin?IDirect3DDevice9_BeginScene(device):IDirect3DDevice9_EndScene(device));}
static int constants_upload(void *ctx,enum fnv_d3d9_stage stage,enum fnv_d3d9_constant_kind kind,uint32_t first,uint32_t count,const uint32_t *bits){
    if(failed_constants)return 0;constant_uploads++;return upload(ctx,stage,kind,first,count,bits);}
static uint32_t draw(void *ctx,int indexed,uint32_t primitive,uint32_t base_bits,uint32_t min_vertex,uint32_t vertices,uint32_t start,uint32_t primitives){
    (void)ctx;if(failed_draw)return 0x80004005;INT base;memcpy(&base,&base_bits,4);
    HRESULT r=indexed?IDirect3DDevice9_DrawIndexedPrimitive(device,primitive,base,min_vertex,vertices,start,primitives):IDirect3DDevice9_DrawPrimitive(device,primitive,start,primitives);
    if(SUCCEEDED(r))draws++;return (uint32_t)r;}
static HWND setup_draw(const char *fixture,int provider) {
    compiler=LoadLibraryExA("d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(compiler);pD3DCompile compile=NULL;FARPROC proc=GetProcAddress(compiler,"D3DCompile");assert(proc && sizeof(proc)==sizeof(compile));memcpy(&compile,&proc,sizeof(compile));
    const char *hlsl[2]={"float4 depth:register(c0);struct V{float2 p:POSITION;float2 uv:TEXCOORD0;};struct O{float4 p:POSITION;float2 uv:TEXCOORD0;};O main(V v){O o;o.p=float4(v.p,depth.z,1);o.uv=v.uv;return o;}",
        "sampler2D image:register(s0);float4 tint:register(c0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv)*tint;}"};
    catalog_count=2;catalog=calloc(2,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<2;i++){ID3DBlob *errors=NULL;HRESULT h=compile(hlsl[i],strlen(hlsl[i]),"own-pipeline-test",NULL,NULL,"main",i?"ps_3_0":"vs_3_0",0,0,shader_blobs+i,&errors);
        if(FAILED(h) && errors)fprintf(stderr,"%s\n",(char*)ID3D10Blob_GetBufferPointer(errors));if(errors)ID3D10Blob_Release(errors);assert(SUCCEEDED(h));
        catalog[i]=(struct fnv_d3d9_shader_code){ID3D10Blob_GetBufferPointer(shader_blobs[i]),(uint32_t)ID3D10Blob_GetBufferSize(shader_blobs[i])};}
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(fixture,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config cfg={0};cfg.memory=arena;cfg.guest_base=BASE;cfg.memory_bytes=ARENA;cfg.code_low=BASE+0x1000;cfg.code_high=BASE+0x3000;assert(fnv_cpu_create(&cfg,&cpu)==FNV_CPU_OK);registry_checks();
    HWND w=CreateWindowExA(0,"STATIC","FNV own textured pipeline",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.catalog=catalog,.catalog_count=2,.buffer_address=BASE+0x100000,.buffer_bytes=4u<<20,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,.device_release=drop_device,
        .bind_shader=bind_shader,.create_buffer=create_buffer,.update_buffer=buffer_data,.bind_stream=stream,.bind_indices=indices,.create_declaration=create_decl,.bind_declaration=bind_decl,
        .create_texture=create_tex,.upload_texture=texture_data,.bind_texture=bind_tex,.create_surface=surface,.surface_desc=desc,.set_target=bind_target,.clear=clear,.state=state,
        .scene=scene,.upload_constants=constants_upload,.draw=provider?draw:NULL}};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);code_at=BASE+0x80000;
    struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);return w;
}
static uint32_t make_buffer(int ib,const void *data,uint32_t bytes){uint32_t args[]={device_at,bytes,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,ib?D3DFMT_INDEX16:0,D3DPOOL_DEFAULT,GUEST_OUT,0};assert(!call(ib?CREATE_IB:CREATE_VB,7,args));uint32_t buffer=get(GUEST_OUT);
    uint32_t lock[]={buffer,0,0,GUEST_OUT,D3DLOCK_DISCARD};assert(!call(LOCK,5,lock));memcpy(ptr(get(GUEST_OUT)),data,bytes);assert(!call(UNLOCK,1,&buffer));return buffer;}
static void tint(float r,float g,float b,float z){float values[4]={r,g,b,1};memcpy(ptr(DATA),values,16);uint32_t c[]={device_at,0,DATA,1};assert(!call(PS_SF,4,c));values[2]=z;memcpy(ptr(DATA),values,16);assert(!call(VS_SF,4,c));}
static void set_render(uint32_t name,uint32_t value){uint32_t args[]={device_at,name,value};assert(!call(RENDER,3,args));}
static void fill_texture(uint32_t texture,uint32_t color){uint32_t lock[]={texture,0,GUEST_OUT,0,0};assert(!call(LOCK_TEX,5,lock));uint32_t pitch=get(GUEST_OUT),at=get(GUEST_OUT+4);
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)put(at+y*pitch+x*4,color);uint32_t unlock[]={texture,0};assert(!call(UNLOCK_TEX,2,unlock));}
static void check_pixels(uint32_t color){IDirect3DSurface9 *readback=NULL;assert(SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,NULL)));
    assert(SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(device,(IDirect3DSurface9*)target,readback)));D3DLOCKED_RECT lock;assert(SUCCEEDED(IDirect3DSurface9_LockRect(readback,&lock,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){DWORD value;memcpy(&value,(char*)lock.pBits+y*lock.Pitch+x*4,4);assert(value==color);pixels++;}
    assert(SUCCEEDED(IDirect3DSurface9_UnlockRect(readback)));IDirect3DSurface9_Release(readback);}
int main(int argc,char **argv){assert(argc==4);if(strcmp(argv[3],"normal") && strcmp(argv[3],"draw_provider"))return baseline_main(argc,argv);
    HWND w=setup_draw(argv[1],strcmp(argv[3],"draw_provider")!=0);uint32_t dip[]={device_at,D3DPT_TRIANGLELIST,UINT32_MAX,1,6,0,2};
    if(!strcmp(argv[3],"draw_provider")){struct fnv_cpu_fault fault;assert(invoke(DRAW_INDEXED,7,dip,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);}
    else {
        uint32_t vs=make_shader(0),ps=make_shader(1),bind[]={device_at,vs};assert(!call(UNSUPPORTED,2,bind));bind[1]=ps;assert(!call(SET_PS,2,bind));
        const float quad[6][4]={{-1,1,0,0},{1,1,1,0},{1,-1,1,1},{-1,1,0,0},{1,-1,1,1},{-1,-1,0,1}};const uint16_t ix[]={1,2,3,4,5,6};
        uint32_t vb=make_buffer(0,quad,sizeof(quad)),ib=make_buffer(1,ix,sizeof(ix));uint32_t stream_args[]={device_at,0,vb,0,16},indices_args[]={device_at,ib};assert(!call(STREAM,5,stream_args) && !call(INDICES,2,indices_args));
        D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_POSITION,0},{0,8,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};memcpy(ptr(DATA),decl,sizeof(decl));uint32_t dc[]={device_at,DATA,GUEST_OUT};assert(!call(CREATE_DECL,3,dc));uint32_t declaration=get(GUEST_OUT);bind[1]=declaration;assert(!call(SET_DECL,2,bind));
        uint32_t tc[]={device_at,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,GUEST_OUT,0};assert(!call(CREATE_TEX,9,tc));uint32_t texture=get(GUEST_OUT);fill_texture(texture,0xFFFFFFFF);uint32_t tb[]={device_at,0,texture};assert(!call(SET_TEX,3,tb));
        uint32_t rc[]={device_at,32,32,D3DFMT_A8R8G8B8,0,0,0,GUEST_OUT,0};assert(!call(CREATE_RT,9,rc));uint32_t rt=get(GUEST_OUT),rtb[]={device_at,0,rt};assert(!call(SET_RT,3,rtb));rc[3]=D3DFMT_D24S8;assert(!call(CREATE_DS,9,rc));uint32_t ds=get(GUEST_OUT),dsb[]={device_at,ds};assert(!call(SET_DS,2,dsb));
        set_render(D3DRS_CULLMODE,D3DCULL_NONE);set_render(D3DRS_ZENABLE,D3DZB_TRUE);set_render(D3DRS_ZWRITEENABLE,1);set_render(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);set_render(D3DRS_SRGBWRITEENABLE,0);
        uint32_t sample[]={device_at,0,D3DSAMP_MINFILTER,D3DTEXF_POINT};assert(!call(SAMPLER,4,sample));sample[2]=D3DSAMP_MAGFILTER;assert(!call(SAMPLER,4,sample));sample[2]=D3DSAMP_MIPFILTER;sample[3]=D3DTEXF_NONE;assert(!call(SAMPLER,4,sample));
        uint32_t clear_args[]={device_at,0,0,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xFF000000,0x3F800000,0};assert(!call(CLEAR,7,clear_args));
        assert(call(DRAW_INDEXED,7,dip)==FNV_D3D9_INVALIDCALL && !draws);
        assert(call(END,1,&device_at)==FNV_D3D9_INVALIDCALL);failed_scene=1;assert(call(BEGIN,1,&device_at)==0x80004005);failed_scene=0;
        assert(!call(BEGIN,1,&device_at) && call(BEGIN,1,&device_at)==FNV_D3D9_INVALIDCALL);
        tint(1,0,0,0.75f);failed_constants=1;assert(call(DRAW_INDEXED,7,dip)==FNV_D3D9_UPLOAD_FAILED && !draws);failed_constants=0;
        assert(!call(DRAW_INDEXED,7,dip));failed_scene=1;assert(call(END,1,&device_at)==0x80004005);failed_scene=0;assert(!call(END,1,&device_at));check_pixels(0xFFFF0000);
        assert(!call(BEGIN,1,&device_at));tint(0,1,0,0.25f);failed_draw=1;unsigned before=draws;assert(call(DRAW_INDEXED,7,dip)==0x80004005 && draws==before);failed_draw=0;
        assert(!call(DRAW_INDEXED,7,dip) && !call(END,1,&device_at));check_pixels(0xFF00FF00);
        fill_texture(texture,0xFF0000FF);tint(1,1,1,0.9f);uint32_t dp[]={device_at,D3DPT_TRIANGLELIST,0,2};assert(!call(BEGIN,1,&device_at));assert(!call(DRAW,4,dp) && !call(END,1,&device_at));check_pixels(0xFF00FF00);
        set_render(D3DRS_ZENABLE,0);assert(!call(BEGIN,1,&device_at));assert(!call(DRAW,4,dp) && !call(END,1,&device_at));check_pixels(0xFF0000FF);
        uint32_t locked[]={vb,0,16,GUEST_OUT,0};assert(!call(LOCK,5,locked) && !call(BEGIN,1,&device_at));before=draws;assert(call(DRAW,4,dp)==FNV_D3D9_INVALIDCALL && draws==before);assert(!call(UNLOCK,1,&vb));assert(!call(END,1,&device_at));
        uint32_t locked_tex[]={texture,0,GUEST_OUT,0,D3DLOCK_READONLY};assert(!call(LOCK_TEX,5,locked_tex) && !call(BEGIN,1,&device_at));before=draws;assert(call(DRAW,4,dp)==FNV_D3D9_INVALIDCALL && draws==before);uint32_t tu[]={texture,0};assert(!call(UNLOCK_TEX,2,tu) && !call(END,1,&device_at));
        uint32_t gs[]={device_at,0,GUEST_OUT,GUEST_OUT+4,GUEST_OUT+8};assert(!call(GET_STREAM,5,gs) && get(GUEST_OUT)==vb && get(GUEST_OUT+8)==16);release(vb);uint32_t gi[]={device_at,GUEST_OUT};assert(!call(GET_INDICES,2,gi) && get(GUEST_OUT)==ib);release(ib);
        assert(!release(vs) && !release(ps) && !release(vb) && !release(ib) && !release(declaration) && !release(texture) && !release(rt) && !release(ds));
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK && !final_device_refs);assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(w);for(unsigned i=0;i<2;i++)ID3D10Blob_Release(shader_blobs[i]);free(catalog);FreeLibrary(compiler);
    printf("{\"status\":\"PASS\",\"prepared_methods\":67,\"draws\":%u,\"constant_uploads\":%u,\"pixel_checks\":%u,\"gpu_rendered\":%s,\"gpu_draw_executed\":%s,\"game_frame_rendered\":false}\n",draws,constant_uploads,pixels,draws?"true":"false",draws?"true":"false");return 0;
}
