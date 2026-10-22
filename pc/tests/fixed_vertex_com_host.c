/* End-to-end synthetic translated COM callers -> fixed/programmable Vulkan. */
#define main unused_nullref_baseline
#include "d3d9_com_host.c"
#undef main
#include "d3d9_vk_backend.h"
#include "d3d9_fixed_vertex.h"
#include "fixed_vertex_fixture.h"
enum {FIX_PS=20,FIX_FVF,FIX_VB,FIX_LOCK,FIX_UNLOCK,FIX_STREAM,FIX_RENDER,FIX_BEGIN,FIX_END,FIX_DRAW,FIX_MATRIX};
static unsigned char *fixed_load(const char *path,uint32_t *size){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*size=(uint32_t)n;return p;
}
int main(int argc,char **argv){
    assert(argc==8);
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));assert(gp);
    struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
    catalog_count=2;catalog=calloc(2,sizeof(*catalog));assert(catalog);
    unsigned char *code[2];uint32_t cb[2];
    for(unsigned i=0;i<2;i++){
        code[i]=fixed_load(argv[4+i],cb+i);catalog[i]=(struct fnv_d3d9_shader_code){code[i],cb[i]};
        uint32_t bytes;unsigned char *spv=fixed_load(argv[6+i],&bytes);struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        assert(!fnv_vk_backend_register_shader(gpu,i,code[i],cb[i],spv,bytes,&plan));free(spv);
    }
    assert(!fnv_vk_backend_link_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,2,0));
    uintptr_t prepared;assert(!b.create(gpu,0,code[0],cb[0],&prepared));assert(!fnv_vk_backend_fixed_vertex(gpu,prepared));b.release(gpu,prepared);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_cpu_config cpu_cfg={0};cpu_cfg.memory=arena;cpu_cfg.guest_base=BASE;cpu_cfg.memory_bytes=ARENA;cpu_cfg.code_low=BASE+0x1000;cpu_cfg.code_high=BASE+0x3000;
    assert(!fnv_cpu_create(&cpu_cfg,&cpu));code_at=BASE+0x200000;
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,
        .backend=b,.catalog=catalog,.catalog_count=2,.buffer_address=BASE+0x1000000,.buffer_bytes=4096};
    assert(!fnv_d3d9_com_create(cpu,&cc,&com));device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));
    uint32_t vs=make_shader(0),ps=make_shader(1),a[7]={device_at,ps};assert(!call(FIX_PS,2,a));
    a[1]=0x102;assert(!call(FIX_FVF,2,a));
    float vertices[30]={-1,1,.5f,.25f,.75f,1,1,.5f,.25f,.75f,1,-1,.5f,.25f,.75f,
        -1,1,.5f,.25f,.75f,1,-1,.5f,.25f,.75f,-1,-1,.5f,.25f,.75f};
    uint32_t create[]={device_at,sizeof(vertices),0,0,1,GUEST_OUT,0};assert(!call(FIX_VB,7,create));uint32_t vb=get(GUEST_OUT);
    uint32_t lock[]={vb,0,sizeof(vertices),GUEST_OUT,0};assert(!call(FIX_LOCK,5,lock));memcpy(ptr(get(GUEST_OUT)),vertices,sizeof(vertices));assert(!call(FIX_UNLOCK,1,&vb));
    uint32_t stream[]={device_at,0,vb,0,20};assert(!call(FIX_STREAM,5,stream));
    uint32_t render[]={device_at,22,1};assert(!call(FIX_RENDER,3,render));render[1]=7;render[2]=0;assert(!call(FIX_RENDER,3,render));
    render[1]=137;assert(!call(FIX_RENDER,3,render));
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};memcpy(ptr(DATA),identity,64);
    uint32_t upload_args[]={device_at,0,DATA,4};assert(!call(VS_SF,4,upload_args));
    unsigned checks=0;
    for(unsigned frame=0;frame<3;frame++){
        struct fnv_transform_state bank;fnv_transform_init(&bank);
        if(frame==1){bank.matrices[10][0]=bank.matrices[10][5]=.5f;bank.matrices[10][12]=.25f;
            bank.matrices[0][12]=-.25f;bank.matrices[0][13]=.25f;bank.matrices[1][0]=.5f;}
        const unsigned states[3]={256,2,3},indices[3]={10,0,1};
        for(unsigned i=0;i<3;i++){
            memcpy(ptr(DATA),!strcmp(argv[3],"ignore-matrices")?identity:bank.matrices[indices[i]],64);
            uint32_t set[]={device_at,states[i],DATA};assert(!call(FIX_MATRIX,3,set));
        }
        if(frame==1 && !strcmp(argv[3],"clobber-constants")){
            float columns[16];assert(!fnv_fixed_vertex_columns(&bank,columns));memcpy(ptr(DATA),columns,64);assert(!call(VS_SF,4,upload_args));
        }
        a[0]=device_at;a[1]=frame==1?0:vs;assert(!call(UNSUPPORTED,2,a));
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
        assert(!call(FIX_BEGIN,1,&device_at));uint32_t draw[]={device_at,4,0,2};assert(!call(FIX_DRAW,4,draw));assert(!call(FIX_END,1,&device_at));
        /* Guest matrix storage is not retained by a queued draw. */
        memset(ptr(DATA),0,64);uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char pixels[4096];
        assert(!fnv_vk_backend_read_color(gpu,color,pixels,sizeof(pixels),10000000000ull));b.release(gpu,color);
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
            int inside=frame!=1 || (x>=12 && x<20 && y>=4 && y<20);
            const unsigned char expected[4]={0,inside?191:0,inside?64:0,255};
            for(unsigned c=0;c<4;c++)if(abs((int)pixels[(y*32+x)*4+c]-expected[c])>1){
                fprintf(stderr,"translated fixed pixel mismatch frame=%u x=%u y=%u channel=%u\n",frame,x,y,c);return 1;
            }
            checks++;
        }
    }
    assert(!release(vs) && !release(ps) && !release(vb));assert(!fnv_cpu_detach() && !fnv_d3d9_com_destroy(com));
    assert(!fnv_cpu_destroy(cpu) && VirtualFree(arena,0,MEM_RELEASE));assert(!fnv_vk_backend_destroy(gpu));
    free(code[0]);free(code[1]);free(catalog);FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"gpu_rendered\":true,\"pixel_checks\":%u,\"translated_fixed_draw\":true,\"game_frame_rendered\":false}\n",checks);return 0;
}
