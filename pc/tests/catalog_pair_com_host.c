/* Original or own catalog shaders, synthetic x86 callers -> COM -> Vulkan. */
#define main unused_nullref_baseline
#include "d3d9_com_host.c"
#undef main
#include "d3d9_vk_prepared.h"
#include "catalog_pair_fixture.h"
#include "catalog_pair_oracle.h"
enum { CP_VS=20,CP_PS,CP_DECL,CP_BINDDECL,CP_VB,CP_LOCK,CP_UNLOCK,CP_STREAM,
       CP_TEX,CP_TLOCK,CP_TUNLOCK,CP_TBIND,CP_RENDER,CP_CLEAR,CP_VIEWPORT,
       CP_BEGIN,CP_END,CP_DRAW,CP_GETVS,CP_GETPS,CP_SAMPLER,CP_GETSAMPLER,
       CP_RT_SURFACE,CP_RT_SET,CP_RT_GET,CP_RT_CONTAINER,CP_RT_DESC };
int main(int argc,char **argv){
    assert(argc==8);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));
    struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
#if defined(FNV_CATALOG_TWO_TEXTURES) || defined(FNV_CATALOG_RENDER_TEXTURE)
    assert(!fnv_vk_backend_draw_wait(gpu,10000000000ull));
#endif
    catalog_count=2;
#ifdef FNV_CATALOG_SHARED_PROGRAM
    catalog_count=3;
#endif
    catalog=calloc(catalog_count,sizeof(*catalog));assert(catalog);
    struct fnv_vk_prepared_program prepared={.inputs=vertex_inputs,.input_count=2,.sampler_mask=FNV_CATALOG_SAMPLER_MASK};
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];
    for(unsigned i=0;i<2;i++){
        code[i]=load(argv[4+i],cb+i);spv[i]=load(argv[6+i],sb+i);catalog[i]=(struct fnv_d3d9_shader_code){code[i],cb[i]};
        prepared.stages[i]=(struct fnv_vk_prepared_stage){code[i],spv[i],cb[i],sb[i],i?pixel_plan:vertex_plan};
    }
    assert(!fnv_vk_prepare_program(gpu,&prepared,catalog,2));
#ifdef FNV_CATALOG_SHARED_PROGRAM
    catalog[2]=(struct fnv_d3d9_shader_code){shared_ps,sizeof(shared_ps)};
    struct fnv_vk_prepared_program other=prepared;other.stages[1]=(struct fnv_vk_prepared_stage){shared_ps,shared_ps_spv,sizeof(shared_ps),sizeof(shared_ps_spv),shared_pixel_plan};
#ifdef FNV_CATALOG_PROGRAM_VARIANT
    other.stages[0].spirv=shared_vs_spv;other.stages[0].spirv_bytes=sizeof(shared_vs_spv);other.stages[0].plan=shared_vertex_plan;other.inputs=shared_vertex_inputs;
#endif
    assert(!fnv_vk_prepare_program(gpu,&other,catalog,catalog_count) && !fnv_vk_prepare_program(gpu,&prepared,catalog,catalog_count));
    struct fnv_vk_stats prepared_stats;assert(!fnv_vk_backend_stats(gpu,&prepared_stats) && prepared_stats.shader_catalog_entries==FNV_SHARED_STAGE_COUNT);
#endif
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_cpu_config cpu_cfg={0};cpu_cfg.memory=arena;cpu_cfg.guest_base=BASE;cpu_cfg.memory_bytes=ARENA;cpu_cfg.code_low=BASE+0x1000;cpu_cfg.code_high=BASE+0x3000;
    assert(!fnv_cpu_create(&cpu_cfg,&cpu));code_at=BASE+0x200000;
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,
        .backend=b,.catalog=catalog,.catalog_count=catalog_count,.buffer_address=BASE+0x1000000,.buffer_bytes=4096};
    assert(!fnv_d3d9_com_create(cpu,&cc,&com));device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));
    uint32_t vs=make_shader(0),ps=make_shader(1),binding[]={device_at,vs};assert(!call(CP_VS,2,binding));binding[1]=ps;assert(!call(CP_PS,2,binding));
#ifdef FNV_CATALOG_SHARED_PROGRAM
    uint32_t other_ps=make_shader(2);
#endif
    memset(ptr(code_at),0,cb[1]); /* Shader creation must copy the guest source. */
    for(unsigned i=0;i<2;i++){
        uint32_t args[]={i?ps:vs,0,GUEST_OUT};put(GUEST_OUT,0);assert(!call(GET_FUNCTION,3,args) && get(GUEST_OUT)==cb[i]);
        args[1]=code_at;assert(!call(GET_FUNCTION,3,args) && !memcmp(ptr(code_at),code[i],cb[i]));memset(ptr(code_at),0,cb[i]);
    }
    memcpy(ptr(DATA),elements,sizeof(elements));uint32_t decl_args[]={device_at,DATA,GUEST_OUT};assert(!call(CP_DECL,3,decl_args));uint32_t decl=get(GUEST_OUT);
    binding[1]=decl;assert(!call(CP_BINDDECL,2,binding));
    uint32_t create[]={device_at,144,0,0,1,GUEST_OUT,0};assert(!call(CP_VB,7,create));uint32_t vb=get(GUEST_OUT);
    uint32_t stream[]={device_at,0,vb,0,24};assert(!call(CP_STREAM,5,stream));
    uint32_t texture_args[]={device_at,2,2,1,0,21,1,GUEST_OUT,0};
#ifdef FNV_CATALOG_MIPS
    texture_args[1]=texture_args[2]=8;texture_args[3]=4;
#endif
    assert(!call(CP_TEX,9,texture_args));uint32_t texture=get(GUEST_OUT);
#ifdef FNV_CATALOG_RENDER_TEXTURE
    uint32_t rt_create[]={device_at,32,32,1,1,21,0,GUEST_OUT,0};assert(!call(CP_TEX,9,rt_create));uint32_t rt_texture=get(GUEST_OUT);
    uint32_t rt_level[]={rt_texture,0,GUEST_OUT};assert(!call(CP_RT_SURFACE,3,rt_level));uint32_t rt_surface=get(GUEST_OUT);
    rt_level[1]=1;assert(call(CP_RT_SURFACE,3,rt_level)==FNV_VK_INVALID && !get(GUEST_OUT));rt_level[1]=0;
    assert(!call(CP_RT_SURFACE,3,rt_level) && get(GUEST_OUT)==rt_surface && release(rt_surface)==1);
    uint32_t rt_desc[]={rt_surface,DATA};assert(!call(CP_RT_DESC,2,rt_desc) && get(DATA)==21 && get(DATA+8)==1 && get(DATA+24)==32 && get(DATA+28)==32);
    /* The surface retains the container even after its public count reaches 0. */
    assert(!release(rt_texture));memcpy(ptr(DATA),&IID_IDirect3DTexture9,16);uint32_t container[]={rt_surface,DATA,GUEST_OUT};assert(!call(CP_RT_CONTAINER,3,container) && get(GUEST_OUT)==rt_texture);
    uint32_t rt_lock[]={rt_texture,0,DATA,0,0};memset(ptr(DATA),0xa5,8);assert(call(CP_TLOCK,5,rt_lock)==FNV_VK_INVALID && get(DATA)==0xa5a5a5a5 && get(DATA+4)==0xa5a5a5a5);
    uint32_t get_back[]={device_at,0,GUEST_OUT};assert(!call(CP_RT_GET,3,get_back));uint32_t back=get(GUEST_OUT);
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
    assert(!call(CP_TEX,9,texture_args));uint32_t secondary=get(GUEST_OUT);
#endif
    uint32_t texbind[]={device_at,0,texture};assert(!call(CP_TBIND,3,texbind));
    uint32_t render[]={device_at,22,1};assert(!call(CP_RENDER,3,render));render[1]=7;render[2]=0;assert(!call(CP_RENDER,3,render));
    const float xy[12]={-1,1,1,1,1,-1,-1,1,1,-1,-1,-1};unsigned checks=0;
    for(unsigned frame=0;frame<6;frame++){
        float vc[1024]={0},pc[896]={0};vc[0]=vc[5]=(frame&1)?.75f:1;vc[10]=vc[15]=1;vc[12]=(frame&2)?.125f:0;
        for(unsigned i=0;i<224;i++){pc[i*4]=.5f;pc[i*4+1]=.75f;pc[i*4+2]=(frame&1)?.25f:1;pc[i*4+3]=.625f;}
        float vertices[36];unsigned char texels[16]={31,83,211,193,173,47,109,127,71,229,53,89,251,139,17,241};
        for(unsigned i=0;i<16;i++)texels[i]^=(unsigned char)(frame*7);
        for(unsigned i=0;i<6;i++){
            vertices[i*6]=xy[i*2];vertices[i*6+1]=xy[i*2+1];vertices[i*6+2]=.5f;vertices[i*6+3]=1;
            vertices[i*6+4]=frame>=4?(xy[i*2]+1)*.5f:((frame&1)?.75f:.25f);vertices[i*6+5]=frame>=4?(1-xy[i*2+1])*.5f:((frame&2)?.75f:.25f);
#ifdef FNV_CATALOG_MIPS
            if(frame==2 || frame==4){float scale=frame==4?22.627417f:16;vertices[i*6+4]=(xy[i*2]+1)*.5f*scale;vertices[i*6+5]=(1-xy[i*2+1])*.5f*scale;}
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
            if(frame>=4){vertices[i*6+4]=xy[i*2]+1;vertices[i*6+5]=1-xy[i*2+1];}
#endif
        }
        const unsigned char *reference_ps=code[1];uint32_t current_ps=ps;
#ifdef FNV_CATALOG_SHARED_PROGRAM
        if(frame&1){reference_ps=(const unsigned char*)shared_ps;if(strcmp(argv[3],"wrong-shared-program"))current_ps=other_ps;}
        binding[1]=current_ps;assert(!call(CP_PS,2,binding));
#endif
        unsigned char expected[4096];oracle(frame,code[0],reference_ps,vertices,texels,vc,pc,expected);
#ifdef FNV_CATALOG_TWO_TEXTURES
        unsigned char second[16];secondary_texels(texels,second);uint32_t second_lock[]={secondary,0,GUEST_OUT,0,0};assert(!call(CP_TLOCK,5,second_lock));
        uint32_t second_pitch=get(GUEST_OUT),second_at=get(GUEST_OUT+4);for(unsigned y=0;y<2;y++)memcpy(ptr(second_at+y*second_pitch),second+y*8,8);
        uint32_t second_unlock[]={secondary,0};assert(!call(CP_TUNLOCK,2,second_unlock));
        int swapped=!strcmp(argv[3],"swapped-textures");texbind[1]=0;texbind[2]=swapped?secondary:texture;assert(!call(CP_TBIND,3,texbind));
        texbind[1]=1;texbind[2]=swapped?texture:secondary;assert(!call(CP_TBIND,3,texbind));
        uint32_t address[]={device_at,1,1,!strcmp(argv[3],"wrong-second-address")?1:3};assert(!call(CP_SAMPLER,4,address));address[2]=2;assert(!call(CP_SAMPLER,4,address));
        uint32_t address_get[]={device_at,1,1,GUEST_OUT};assert(!call(CP_GETSAMPLER,4,address_get) && get(GUEST_OUT)==address[3]);
#endif
        if(!strcmp(argv[3],"wrong-uv"))for(unsigned i=0;i<6;i++)vertices[i*6+4]=.75f;
        if(!strcmp(argv[3],"wrong-texture"))for(unsigned i=0;i<4;i++){unsigned char t=texels[i*4];texels[i*4]=texels[i*4+2];texels[i*4+2]=t;}
        if(!strcmp(argv[3],"wrong-vs-constants"))memset(vc,0,sizeof(vc));
        if(!strcmp(argv[3],"wrong-ps-constants"))memset(pc,0,sizeof(pc));
        memcpy(ptr(DATA),vc,sizeof(vc));uint32_t constants[]={device_at,0,DATA,256};assert(!call(VS_SF,4,constants));
        memcpy(ptr(DATA),pc,sizeof(pc));constants[3]=224;assert(!call(PS_SF,4,constants));memset(ptr(DATA),0,4096);
        uint32_t lock[]={vb,0,144,GUEST_OUT,0};assert(!call(CP_LOCK,5,lock));memcpy(ptr(get(GUEST_OUT)),vertices,144);assert(!call(CP_UNLOCK,1,&vb));
        uint32_t tlock[]={texture,0,GUEST_OUT,0,0};
#ifdef FNV_CATALOG_MIPS
        for(unsigned level=0;level<4;level++){
            tlock[1]=level;assert(!call(CP_TLOCK,5,tlock));uint32_t pitch=get(GUEST_OUT),bits=get(GUEST_OUT+4);unsigned char data[256];mip_texels(level,data);unsigned width=8u>>level;
            for(unsigned y=0;y<width;y++)memcpy(ptr(bits+y*pitch),data+y*width*4,width*4);
            uint32_t unlock[]={texture,level};assert(!call(CP_TUNLOCK,2,unlock));assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
        }
        uint32_t mip[]={device_at,0,9,!strcmp(argv[3],"wrong-mip-clamp")?0:mip_clamp[frame]};assert(!call(CP_SAMPLER,4,mip));
        uint32_t getter[]={device_at,0,9,GUEST_OUT};assert(!call(CP_GETSAMPLER,4,getter) && get(GUEST_OUT)==mip[3]);
        mip[2]=7;mip[3]=!strcmp(argv[3],"wrong-mip-filter")?0:mip_filter[frame];assert(!call(CP_SAMPLER,4,mip));
#else
        assert(!call(CP_TLOCK,5,tlock));uint32_t pitch=get(GUEST_OUT),bits=get(GUEST_OUT+4);
        for(unsigned y=0;y<2;y++)memcpy(ptr(bits+y*pitch),texels+y*8,8);
        uint32_t unlock[]={texture,0};assert(!call(CP_TUNLOCK,2,unlock));
#endif
        uint32_t clear[]={device_at,0,0,1,0xff000000,0,0};assert(!call(CP_CLEAR,7,clear));
#ifdef FNV_CATALOG_RENDER_TEXTURE
        uint32_t rt_bind[]={device_at,0,rt_surface};assert(!call(CP_RT_SET,3,rt_bind));texbind[1]=0;texbind[2]=texture;assert(!call(CP_TBIND,3,texbind));
        assert(!call(CP_CLEAR,7,clear) && !call(CP_BEGIN,1,&device_at));uint32_t first_draw[]={device_at,4,0,2};assert(!call(CP_DRAW,4,first_draw) && !call(CP_END,1,&device_at));
        rt_bind[2]=back;assert(!call(CP_RT_SET,3,rt_bind));texbind[2]=!strcmp(argv[3],"wrong-render-source")?texture:rt_texture;assert(!call(CP_TBIND,3,texbind));
        assert(!call(CP_CLEAR,7,clear));
#endif
        if(frame==5){uint32_t viewport[6]={4,2,24,28,0,0x3f800000};memcpy(ptr(DATA),viewport,24);uint32_t args[]={device_at,DATA};assert(!call(CP_VIEWPORT,2,args));}
#ifndef FNV_CATALOG_RENDER_TEXTURE
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
#endif
        assert(!call(CP_BEGIN,1,&device_at));uint32_t draw[]={device_at,4,0,2};assert(!call(CP_DRAW,4,draw));assert(!call(CP_END,1,&device_at));
#ifdef FNV_CATALOG_MIPS
        uint32_t reset_sampler[]={device_at,0,9,0};assert(!call(CP_SAMPLER,4,reset_sampler));reset_sampler[2]=7;assert(!call(CP_SAMPLER,4,reset_sampler));
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
        uint32_t unbind[]={device_at,1,0};assert(!call(CP_TBIND,3,unbind));
#endif
        /* Change the COM banks after draw: the submitted GPU snapshot survives. */
        memset(ptr(DATA),0,4096);constants[3]=256;assert(!call(VS_SF,4,constants));constants[3]=224;assert(!call(PS_SF,4,constants));
        for(unsigned i=0;i<2;i++){uint32_t getter[]={device_at,GUEST_OUT};assert(!call(i?CP_GETPS:CP_GETVS,2,getter));uint32_t observed=get(GUEST_OUT);assert(observed==(i?current_ps:vs));assert(release(observed)==1);}
        uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char actual[4096];assert(!fnv_vk_backend_read_color(gpu,color,actual,sizeof(actual),10000000000ull));b.release(gpu,color);
        for(unsigned i=0;i<4096;i++)if(abs((int)actual[i]-(int)expected[i])>1){fprintf(stderr,"translated catalog pixel mismatch frame=%u byte=%u\n",frame,i);return 2;}
        checks+=1024;
    }
#ifdef FNV_CATALOG_TWO_TEXTURES
    struct fnv_vk_stats stats;assert(!fnv_vk_backend_stats(gpu,&stats) && stats.draw_waits>=6);assert(!release(secondary));
#endif
#ifdef FNV_CATALOG_RENDER_TEXTURE
    assert(!release(rt_surface) && !release(rt_texture) && !release(back));
#endif
#ifdef FNV_CATALOG_SHARED_PROGRAM
    assert(!release(other_ps));
#endif
    assert(!release(vs) && !release(ps) && !release(vb) && !release(texture) && !release(decl));
    assert(!fnv_cpu_detach() && !fnv_d3d9_com_destroy(com) && !fnv_cpu_destroy(cpu));assert(VirtualFree(arena,0,MEM_RELEASE));assert(!fnv_vk_backend_destroy(gpu));
    for(unsigned i=0;i<2;i++){free(code[i]);free(spv[i]);}free(catalog);FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"gpu_rendered\":true,\"pixel_checks\":%u,\"translated_catalog_draw\":true,\"game_frame_rendered\":false}\n",checks);return 0;
}
