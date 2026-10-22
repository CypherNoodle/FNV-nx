/* End-to-end synthetic translated COM callers -> fixed/programmable Vulkan. */
#define main unused_nullref_baseline
#include "d3d9_com_host.c"
#undef main
#include "d3d9_vk_backend.h"
#include "d3d9_directional_vertex.h"
#include "directional_vertex_fixture.h"
enum {FIX_PS=20,FIX_FVF,FIX_VB,FIX_LOCK,FIX_UNLOCK,FIX_STREAM,FIX_RENDER,FIX_BEGIN,FIX_END,FIX_DRAW,FIX_MATRIX,FIX_MATERIAL,FIX_LIGHT,FIX_ENABLE,FIX_TEXTURE,FIX_TLOCK,FIX_TUNLOCK,FIX_TBIND};
#ifdef FNV_FIXED_AUTOMATIC
#define FIX_STAGE_SET (34+4*SAMPLER_MASK)
#define FIX_STAGE_GET (35+4*SAMPLER_MASK)
#define FIX_PS_GET (36+4*SAMPLER_MASK)
#define FIX_BLOCK_BEGIN (37+4*SAMPLER_MASK)
#define FIX_BLOCK_END (38+4*SAMPLER_MASK)
#define FIX_BLOCK_APPLY (40+4*SAMPLER_MASK)
#define FIX_BLOCK_CAPTURE (39+4*SAMPLER_MASK)
#endif
static unsigned char *fixed_load(const char *path,uint32_t *size){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*size=(uint32_t)n;return p;
}
#include "directional_vertex_oracle.h"
#ifdef FNV_DIRECTIONAL_TEXTURED
#define INPUT_COUNT 3
#define SAMPLER_MASK 1
static void com_texture_upload(uint32_t texture,int opaque,int poison){
    uint32_t lock[]={texture,0,GUEST_OUT,0,0};assert(!call(FIX_TLOCK,5,lock));
    unsigned pitch=get(GUEST_OUT);unsigned char *p=ptr(get(GUEST_OUT+4));
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){
        unsigned char *dst=p+y*pitch+x*4;memcpy(dst,directional_texels+(y*2+x)*4,4);
        if(opaque)dst[3]=255;
        if(poison)memset(dst,0,4);
    }
    uint32_t unlock[]={texture,0};assert(!call(FIX_TUNLOCK,2,unlock));
}
#else
#define INPUT_COUNT 2
#define SAMPLER_MASK 0
#endif
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
    assert(!fnv_vk_backend_link_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,INPUT_COUNT,SAMPLER_MASK));
#ifdef FNV_FIXED_AUTOMATIC
    uintptr_t prepared_ps;assert(!b.create(gpu,1,code[1],cb[1],&prepared_ps));
#ifdef FNV_DIRECTIONAL_TEXTURED
#ifdef FNV_FIXED_ALPHA_TEXTURE
    assert(!fnv_vk_backend_fixed_pixel(gpu,2,prepared_ps));
#else
    assert(!fnv_vk_backend_fixed_pixel(gpu,1,prepared_ps));
#endif
#else
    assert(!fnv_vk_backend_fixed_pixel(gpu,0,prepared_ps));
#endif
    b.release(gpu,prepared_ps);
#endif
    uintptr_t prepared;assert(!b.create(gpu,0,code[0],cb[0],&prepared));assert(!fnv_vk_backend_fixed_directional(gpu,prepared));b.release(gpu,prepared);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_cpu_config cpu_cfg={0};cpu_cfg.memory=arena;cpu_cfg.guest_base=BASE;cpu_cfg.memory_bytes=ARENA;cpu_cfg.code_low=BASE+0x1000;cpu_cfg.code_high=BASE+0x3000;
    assert(!fnv_cpu_create(&cpu_cfg,&cpu));code_at=BASE+0x200000;
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,
        .backend=b,.catalog=catalog,.catalog_count=2,.buffer_address=BASE+0x1000000,.buffer_bytes=4096};
    assert(!fnv_d3d9_com_create(cpu,&cc,&com));device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));
    uint32_t vs=make_shader(0),ps=make_shader(1),a[7]={device_at,ps};assert(!call(FIX_PS,2,a));
    a[1]=0x112;assert(!call(FIX_FVF,2,a));
    const float positions[18]={-1,1,.5f,1,1,.5f,1,-1,.5f,-1,1,.5f,1,-1,.5f,-1,-1,.5f};float vertices[48]={0};
    for(unsigned i=0;i<6;i++){memcpy(vertices+i*8,positions+i*3,12);vertices[i*8+3]=1;vertices[i*8+5]=-1;}
    uint32_t create[]={device_at,sizeof(vertices),0,0,1,GUEST_OUT,0};assert(!call(FIX_VB,7,create));uint32_t vb=get(GUEST_OUT);
    uint32_t lock[]={vb,0,sizeof(vertices),GUEST_OUT,0};assert(!call(FIX_LOCK,5,lock));memcpy(ptr(get(GUEST_OUT)),vertices,sizeof(vertices));assert(!call(FIX_UNLOCK,1,&vb));
    uint32_t stream[]={device_at,0,vb,0,32};assert(!call(FIX_STREAM,5,stream));
    uint32_t render[]={device_at,22,1};assert(!call(FIX_RENDER,3,render));render[1]=7;render[2]=0;assert(!call(FIX_RENDER,3,render));
    render[1]=137;render[2]=1;assert(!call(FIX_RENDER,3,render));
    render[1]=141;render[2]=0;assert(!call(FIX_RENDER,3,render));render[1]=143;render[2]=1;assert(!call(FIX_RENDER,3,render));
#ifdef FNV_DIRECTIONAL_TEXTURED
    uint32_t create_texture[]={device_at,2,2,1,0,21,1,GUEST_OUT,0};assert(!call(FIX_TEXTURE,9,create_texture));
    uint32_t texture=get(GUEST_OUT),bind_texture[]={device_at,0,texture};assert(!call(FIX_TBIND,3,bind_texture));
#endif
    unsigned checks=0;
    for(unsigned frame=0;frame<(4+SAMPLER_MASK);frame++){
        struct fnv_transform_state bank;fnv_transform_init(&bank);float material[17]={.5f,.25f,.75f,.6f},light[26]={0};
        light[1]=light[2]=light[3]=1;light[18]=1;uint32_t ambient=0,words[26],mat[17],on=frame!=3;
        if(frame==1){bank.matrices[10][0]=2;material[12]=.0625f;material[13]=.125f;}
        if(frame==3){material[4]=.5f;material[5]=.25f;material[6]=.125f;material[12]=.0625f;material[14]=.125f;ambient=0x204060;}
        memcpy(mat,material,68);memcpy(words,light,104);words[0]=3;
        struct fnv_light_bank lights={0};assert(!fnv_light_change(&lights,NULL,0,words,&on));float constants[40];
        assert(!fnv_fixed_directional_columns(&bank,mat,&lights,ambient,1,constants));fnv_light_destroy(&lights);
        if(!frame){memcpy(ptr(DATA),constants,160);uint32_t upload[]={device_at,0,DATA,10};assert(!call(VS_SF,4,upload));}
#ifdef FNV_DIRECTIONAL_TEXTURED
        directional_uv(frame,vertices);
#endif
        unsigned char expected[4096];oracle(frame,vertices,&bank,mat,words,ambient,expected);
#ifdef FNV_DIRECTIONAL_TEXTURED
        if(!strcmp(argv[3],"wrong-uv"))directional_uv(frame^1,vertices);
        uint32_t vertex_lock[]={vb,0,sizeof(vertices),GUEST_OUT,0};assert(!call(FIX_LOCK,5,vertex_lock));
        memcpy(ptr(get(GUEST_OUT)),vertices,sizeof(vertices));assert(!call(FIX_UNLOCK,1,&vb));
        com_texture_upload(texture,!strcmp(argv[3],"wrong-texture-alpha"),0);
        assert(!fnv_vk_backend_wait(gpu,10000000000ull));
#endif
        const unsigned states[3]={256,2,3},indices[3]={10,0,1};
        for(unsigned i=0;i<3;i++){
            if(frame==1 && !strcmp(argv[3],"ignore-matrices"))continue;
            memcpy(ptr(DATA),bank.matrices[indices[i]],64);uint32_t set[]={device_at,states[i],DATA};assert(!call(FIX_MATRIX,3,set));
        }
        memcpy(ptr(DATA),mat,68);uint32_t set_material[]={device_at,DATA};
        if(frame!=1 || strcmp(argv[3],"ignore-material"))assert(!call(FIX_MATERIAL,2,set_material));
        memcpy(ptr(DATA),words,104);uint32_t set_light[]={device_at,0,DATA},enable[]={device_at,0,on};assert(!call(FIX_LIGHT,3,set_light) && !call(FIX_ENABLE,3,enable));
        render[1]=139;render[2]=ambient;assert(!call(FIX_RENDER,3,render));
        if(frame==1 && !strcmp(argv[3],"clobber-constants")){memcpy(ptr(DATA),constants,160);uint32_t upload[]={device_at,0,DATA,10};assert(!call(VS_SF,4,upload));}
#ifdef FNV_FIXED_AUTOMATIC
        const uint32_t stage_values[6]={
#ifdef FNV_DIRECTIONAL_TEXTURED
            4,2,1,
#ifdef FNV_FIXED_ALPHA_TEXTURE
            2,2,1
#else
            4,2,1
#endif
#else
            2,0,1,2,0,1
#endif
        };
        for(unsigned i=0;i<6;i++){
            uint32_t set[]={device_at,0,i+1,stage_values[i]},get_stage[]={device_at,0,i+1,GUEST_OUT};
            assert(!call(FIX_STAGE_SET,4,set) && !call(FIX_STAGE_GET,4,get_stage) && get(GUEST_OUT)==stage_values[i]);
        }
        if(!frame){
            uint32_t change[]={device_at,1,1,2},read[]={device_at,1,1,GUEST_OUT};
            assert(!call(FIX_BLOCK_BEGIN,1,&device_at));assert(!call(FIX_STAGE_SET,4,change));
            assert(!call(FIX_STAGE_GET,4,read) && get(GUEST_OUT)==1);
            uint32_t finish[]={device_at,GUEST_OUT};assert(!call(FIX_BLOCK_END,2,finish));uint32_t block=get(GUEST_OUT);
            assert(!call(FIX_BLOCK_APPLY,1,&block) && !call(FIX_STAGE_GET,4,read) && get(GUEST_OUT)==2);
            change[3]=1;assert(!call(FIX_STAGE_SET,4,change) && !call(FIX_BLOCK_CAPTURE,1,&block));
            change[3]=2;assert(!call(FIX_STAGE_SET,4,change) && !call(FIX_BLOCK_APPLY,1,&block));
            assert(!call(FIX_STAGE_GET,4,read) && get(GUEST_OUT)==1);assert(!release(block));
        }
        uint32_t pixel_bind[]={device_at,(frame==0 || frame==2)?ps:0};assert(!call(FIX_PS,2,pixel_bind));
#endif
        a[0]=device_at;a[1]=(frame==0 || frame==2)?vs:0;assert(!call(UNSUPPORTED,2,a));
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
        assert(!call(FIX_BEGIN,1,&device_at));uint32_t draw[]={device_at,4,0,2};
#ifdef FNV_DIRECTIONAL_TEXTURED
        if(!frame){
            struct fnv_vk_stats before,after;assert(!fnv_vk_backend_stats(gpu,&before));
            uint32_t empty[]={device_at,0,0};assert(!call(FIX_TBIND,3,empty));
            assert(call(FIX_DRAW,4,draw)==FNV_VK_INVALID);assert(!fnv_vk_backend_stats(gpu,&after));
            assert(before.draws==after.draws && before.submissions==after.submissions);
            assert(!call(FIX_TBIND,3,bind_texture));
        }
#endif
#ifdef FNV_FIXED_AUTOMATIC
        if(frame==1){
            uint32_t change[]={device_at,1,1,2};assert(!call(FIX_STAGE_SET,4,change));
            assert(call(FIX_DRAW,4,draw)==0x8876086au);change[3]=1;assert(!call(FIX_STAGE_SET,4,change));
            change[1]=0;change[3]=99;assert(!call(FIX_STAGE_SET,4,change));assert(call(FIX_DRAW,4,draw)==0x8876086au);
            change[3]=stage_values[0];assert(!call(FIX_STAGE_SET,4,change));
        }
#endif
        assert(!call(FIX_DRAW,4,draw));assert(!call(FIX_END,1,&device_at));
#ifdef FNV_DIRECTIONAL_TEXTURED
        com_texture_upload(texture,0,1);
#endif
#ifdef FNV_FIXED_AUTOMATIC
        uint32_t pixel_get[]={device_at,GUEST_OUT};assert(!call(FIX_PS_GET,2,pixel_get));
        uint32_t observed_ps=get(GUEST_OUT);assert(observed_ps==((frame==0 || frame==2)?ps:0));
        if(observed_ps)assert(release(observed_ps)==1);
#endif
        memset(ptr(DATA),0,160);uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char pixels[4096];
        assert(!fnv_vk_backend_read_color(gpu,color,pixels,sizeof(pixels),10000000000ull));b.release(gpu,color);
        for(unsigned i=0;i<4096;i++){if(abs((int)pixels[i]-expected[i])>1){fprintf(stderr,"translated directional pixel mismatch frame=%u byte=%u actual=%u expected=%u\n",frame,i,pixels[i],expected[i]);return 1;}if(i%4==0)checks++;}
    }
#ifdef FNV_DIRECTIONAL_TEXTURED
    assert(!release(texture));
#endif
    assert(!release(vs) && !release(ps) && !release(vb));assert(!fnv_cpu_detach() && !fnv_d3d9_com_destroy(com));
    assert(!fnv_cpu_destroy(cpu) && VirtualFree(arena,0,MEM_RELEASE));assert(!fnv_vk_backend_destroy(gpu));
    free(code[0]);free(code[1]);free(catalog);FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"gpu_rendered\":true,\"pixel_checks\":%u,\"translated_directional_draw\":true,\"game_frame_rendered\":false}\n",checks);return 0;
}
