#define main oracle_baseline_main
#include "d3d9_com_host.c"
#undef main
#include "d3d9_uniform_state.h"
#include "uniform_fixture.h"
static struct fnv_uniform_state *uniform_state;
static struct fnv_uniform_draw saved[3];
static unsigned char preserved[3][2][64];
static unsigned recorded,submitted_hook,completed_hook,upload_attempts;
static int fail_upload_once;
static int bound[2];
enum {BEGIN=20,END,DRAW_INDEXED,SET_PS};
static void preserve_check(void){
    for(unsigned i=0;i<recorded;++i)for(unsigned s=0;s<2;++s)
        assert(!memcmp(saved[i].data[s],preserved[i][s],saved[i].bytes[s]));
}
static void submitted(void){
    assert(!submitted_hook++ && !completed_hook);preserve_check();
    uint64_t tickets[3]={saved[0].ticket,saved[1].ticket,saved[2].ticket};
    assert(!fnv_uniform_state_commit_batch(uniform_state,tickets,3,1));
    struct fnv_uniform_stats s;assert(!fnv_uniform_state_stats(uniform_state,&s));
    assert(s.submitted_slots==3 && !s.free_slots && !s.reserved_slots);
    struct fnv_uniform_draw none;assert(fnv_uniform_state_prepare(uniform_state,&none)==FNV_UNIFORM_BUSY);
    assert(fnv_uniform_state_destroy(uniform_state)==FNV_UNIFORM_BUSY);
}
static void completed(void){
    assert(submitted_hook==1 && !completed_hook++);preserve_check();
    assert(!fnv_uniform_state_complete(uniform_state,1));
    struct fnv_uniform_stats s;assert(!fnv_uniform_state_stats(uniform_state,&s));
    assert(s.free_slots==3 && !s.submitted_slots && s.retired==3);
}
#define FNV_VULKAN_SUBMITTED() submitted()
#define FNV_VULKAN_COMPLETED() completed()
#define main vulkan_draw_main
#include "vulkan_pipeline_host.c"
#undef main
static uint32_t bind_uniform_shader(void *ctx,enum fnv_d3d9_stage stage,uintptr_t handle){
    (void)ctx;const struct fnv_uniform_plan *next=handle?fixture_plans+stage:NULL;
    uint32_t hr=fnv_uniform_state_bind(uniform_state,stage,next);if(hr)return hr;
    HRESULT r=stage?IDirect3DDevice9_SetPixelShader(device,(IDirect3DPixelShader9*)handle):
        IDirect3DDevice9_SetVertexShader(device,(IDirect3DVertexShader9*)handle);
    if(FAILED(r))assert(!fnv_uniform_state_bind(uniform_state,stage,bound[stage]?fixture_plans+stage:NULL));
    else bound[stage]=handle!=0;
    return (uint32_t)r;
}
static int upload_uniforms(void *ctx,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const uint32_t *data){
    (void)ctx;upload_attempts++;if(fail_upload_once){fail_upload_once=0;return 0;}
    return fnv_uniform_state_upload(uniform_state,s,k,first,count,data);
}
static uint32_t scene_record(void *ctx,int begin){(void)ctx;(void)begin;return 0;}
static uint32_t record_draw(void *ctx,int indexed,uint32_t primitive,uint32_t base,uint32_t minv,uint32_t vertices,uint32_t start,uint32_t primitives){
    (void)ctx;assert(indexed && primitive==4 && base==UINT32_MAX && minv==1 && vertices==4 && !start && primitives==2);
    struct fnv_uniform_draw draw;
    uint32_t r=fnv_uniform_state_prepare(uniform_state,&draw);if(r)return r;
    assert(recorded<3);saved[recorded]=draw;
    for(unsigned s=0;s<2;++s){assert(draw.bytes[s]<=64);memcpy(preserved[recorded][s],draw.data[s],draw.bytes[s]);}
    recorded++;return 0;
}
static void constants_for_frame(unsigned frame){
    float depth[4]={0,0,(frame==0?.75f:frame==1?.25f:.9f)-.0625f,0};
    float adjust[4]={0,0,.0625f,0},bias[4]={0,0,0,1},tint[4]={0,0,0,0};tint[frame]=1;
    const float *values[]={depth,adjust,bias,tint};unsigned regs[]={7,9,3,12};
    for(unsigned i=0;i<4;++i){memcpy(ptr(DATA),values[i],16);uint32_t args[]={device_at,regs[i],DATA,1};assert(!call(i<2?VS_SF:PS_SF,4,args));}
    uint32_t ints[4]={2,0,0,0};memcpy(ptr(DATA),ints,16);uint32_t args[]={device_at,2,DATA,1};assert(!call(VS_SI,4,args));
    put(DATA,UINT32_MAX);args[1]=3;assert(!call(VS_SB,4,args));
    memset(ptr(DATA),0,16);assert(!call(VS_GB,4,args) && get(DATA)==UINT32_MAX);
}
int main(int argc,char **argv){
    assert(argc==7 && !strcmp(argv[3],"normal"));
    struct fnv_uniform_config uc={256,224,3,64};assert(!fnv_uniform_state_create(&uc,&uniform_state));
    const char *paths[2]={FIXTURE_VS,FIXTURE_PS};catalog_count=2;catalog=calloc(2,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<2;++i){FILE *f=fopen(paths[i],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);
        void *data=malloc((size_t)n);assert(data && fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);catalog[i]=(struct fnv_d3d9_shader_code){data,(uint32_t)n};}
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config config={0};config.memory=arena;config.guest_base=BASE;config.memory_bytes=ARENA;
    config.code_low=BASE+0x1000;config.code_high=BASE+0x3000;assert(fnv_cpu_create(&config,&cpu)==FNV_CPU_OK);registry_checks();code_at=BASE+0x80000;
    HWND window=CreateWindowExA(0,"STATIC","FNV COM uniform path",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
    api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=8,
        .vertex_floats=256,.pixel_floats=224,.catalog=catalog,.catalog_count=2,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,
        .device_release=device_release,.bind_shader=bind_uniform_shader,.upload_constants=upload_uniforms,.scene=scene_record,.draw=record_draw}};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&initial)==FNV_CPU_OK);
    uint32_t vs=make_shader(0),ps=make_shader(1),bind_args[]={device_at,vs};assert(!call(UNSUPPORTED,2,bind_args));
    bind_args[1]=ps;assert(!call(SET_PS,2,bind_args));
    uint32_t draw_args[]={device_at,4,UINT32_MAX,1,4,0,2};assert(!call(BEGIN,1,&device_at));
    constants_for_frame(0);fail_upload_once=1;assert(call(DRAW_INDEXED,7,draw_args)==FNV_D3D9_UPLOAD_FAILED && !recorded);
    for(unsigned i=0;i<3;++i){if(i)constants_for_frame(i);preserve_check();assert(!call(DRAW_INDEXED,7,draw_args));}
    assert(!call(END,1,&device_at));assert(call(DRAW_INDEXED,7,draw_args)==FNV_D3D9_INVALIDCALL);
    assert(!call(BEGIN,1,&device_at));assert(call(DRAW_INDEXED,7,draw_args)==FNV_UNIFORM_BUSY);assert(!call(END,1,&device_at));
    struct fnv_uniform_stats stats;assert(!fnv_uniform_state_stats(uniform_state,&stats) && stats.reserved_slots==3);
    FILE *f=fopen(argv[6],"wbx");assert(f);assert(fwrite("FNVUBO01",1,8,f)==8);
    for(unsigned s=0;s<2;++s)assert(fwrite(&saved[0].bytes[s],4,1,f)==1);
    for(unsigned i=0;i<3;++i)for(unsigned s=0;s<2;++s)assert(fwrite(saved[i].data[s],1,saved[i].bytes[s],f)==saved[i].bytes[s]);
    assert(!fclose(f));char *gpu[]={"own-vulkan",argv[4],argv[5],argv[6]};assert(!vulkan_draw_main(4,gpu));
    assert(submitted_hook==1 && completed_hook==1);
    struct fnv_uniform_draw reuse;assert(!fnv_uniform_state_prepare(uniform_state,&reuse));
    assert(reuse.ticket!=saved[0].ticket && fnv_uniform_state_cancel(uniform_state,saved[0].ticket)==FNV_D3D9_INVALIDCALL);
    assert(!fnv_uniform_state_cancel(uniform_state,reuse.ticket));
    bind_args[1]=0;assert(!call(UNSUPPORTED,2,bind_args) && !call(SET_PS,2,bind_args));assert(!release(vs) && !release(ps));
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    for(unsigned i=0;i<2;++i)free((void*)catalog[i].bytes);free(catalog);
    assert(!fnv_uniform_state_destroy(uniform_state));
    printf("{\"status\":\"PASS\",\"prepared_methods\":67,\"recorded_versions\":3,\"uploads_attempted\":%u,"
        "\"actual_gpu_fence_observed\":true,\"pixels_checked\":4096,\"gpu_rendered\":true,\"game_executed\":false}\n",upload_attempts);
    return 0;
}
