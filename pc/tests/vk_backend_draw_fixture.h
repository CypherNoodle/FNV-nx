enum {D_INDEXED=61,D_PRIMITIVE,D_SAMPLER_SET,D_SAMPLER_GET};
static unsigned draw_pixel_checks,draw_build_faults,draw_mirrored;
static void draw_constants(unsigned color,float z){
    float depth[4]={0,0,z-.0625f,0},adjust[4]={0,0,.0625f,0},bias[4]={0,0,0,1},tint[4]={0,0,0,0};tint[color]=1;
    const float *data[4]={depth,adjust,bias,tint};const unsigned regs[4]={7,9,3,12};
    for(unsigned i=0;i<4;++i){memcpy(ptr(DATA),data[i],16);uint32_t args[]={device_at,regs[i],DATA,1};assert(!call(i<2?VS_SF:PS_SF,4,args));}
    uint32_t ints[4]={2,0,0,0};memcpy(ptr(DATA),ints,16);uint32_t args[]={device_at,2,DATA,1};assert(!call(VS_SI,4,args));
    put(DATA,UINT32_MAX);args[1]=3;assert(!call(VS_SB,4,args));
}
static void draw_geometry(uint32_t vb,uint32_t ib,float uv_shift){
    float positions[16]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,-1,0,1};unsigned char data[96]={0};
    for(unsigned i=0;i<4;++i){positions[i*4+2]+=uv_shift;memcpy(data+16+i*16,positions+i*4,16);}
    const uint16_t indices[8]={0,0,1,2,3,1,3,4};upload_buffer(vb,0,96,0,data);upload_buffer(ib,0,16,0,indices);
}
static void draw_texture(uint32_t texture,const unsigned char gray[4]){unsigned pitch;unsigned char *dst=lock_texture(texture,0,0,0,&pitch);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=dst+y*pitch+x*4;p[0]=p[1]=p[2]=gray[y*2+x];p[3]=255;}
    assert(!unlock_texture(texture,0));
}
static void draw_pixels(unsigned channel,const unsigned char gray[4],int patch){
    unsigned char data[4096];assert(!fnv_vk_backend_read_color(gpu,native_color(),data,sizeof(data),10000000000ull));
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){unsigned actual_channel=channel;
        if(patch)actual_channel=x>=8 && x<24 && y>=4 && y<28?0:2;
        unsigned char expected[4]={0,0,0,255};expected[2-actual_channel]=gray[((y>=16)*2+(x>=16))^draw_mirrored];
        if(memcmp(data+(y*32+x)*4,expected,4)){unsigned char *a=data+(y*32+x)*4;fprintf(stderr,"native draw pixel %u,%u BGRA=%u,%u,%u,%u expected=%u,%u,%u,%u\n",x,y,a[0],a[1],a[2],a[3],expected[0],expected[1],expected[2],expected[3]);exit(1);}
        draw_pixel_checks++;}
}
#include "vk_backend_render_states_fixture.h"
#include "vk_backend_bc_fixture.h"
#include "vk_backend_up_fixture.h"
#include "vk_backend_fvf_fixture.h"
#include "vk_backend_stateblocks_fixture.h"
#include "vk_backend_fixed_dispatch_fixture.h"
static void exercise_draws(void){
    unsigned stage_index[2]={0};for(unsigned i=0;i<catalog_count;++i)stage_index[((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff]=i;
    struct fnv_vk_vertex_input map[2]={{0,0,0},{5,0,1}};
    uint32_t vs=make_shader(stage_index[0]),ps=make_shader(stage_index[1]);uint32_t bind[]={device_at,vs};assert(!call(S_VS_BIND,2,bind));bind[1]=ps;assert(!call(S_PS_BIND,2,bind));
    const unsigned char elements[24]={0,0,0,0,1,0,0,0,0,0,8,0,1,0,5,0,255,0,0,0,17,0,0,0};memcpy(ptr(DATA),elements,24);
    uint32_t create[]={device_at,DATA,GUEST_OUT};assert(!call(S_DECL_CREATE,3,create));uint32_t decl=get(GUEST_OUT);bind[1]=decl;assert(!call(S_DECL_BIND,2,bind));
    uint32_t vb=make_buffer(0,96,0,1,0),ib=make_buffer(1,16,0,1,101);
    uint32_t stream[]={device_at,0,vb,16,16},index[]={device_at,ib};assert(!call(B_STREAM,5,stream) && !call(B_INDICES,2,index));draw_geometry(vb,ib,0);
    uint32_t texture=make_texture(2,2,1,0,21,1),texture_bind[]={device_at,0,texture};assert(!call(T_BIND,3,texture_bind));
    const unsigned char first[4]={255,128,64,192},second[4]={21,67,139,233};draw_texture(texture,first);texture_ready(texture);
    uint32_t render[]={device_at,22,1};assert(!call(SET_RENDER,3,render));
    uint32_t clear[]={device_at,0,0,7,0xff000000,0x3f800000,0};assert(!call(CLEAR,7,clear));assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
    /* Real backend rejects the pair before trusted offline linkage, records it
     * once and counts repeats without submitting anything to the GPU. */
    uint32_t missing_args[]={device_at,4,UINT32_MAX,1,4,2,2};
    assert(!call(BEGIN,1,&device_at));
    assert(call(D_INDEXED,7,missing_args)==FNV_VK_INVALID);
    assert(call(D_INDEXED,7,missing_args)==FNV_VK_INVALID);
    struct fnv_vk_stats missing=stats();assert(missing.missing_pairs.count==1 && !missing.missing_pairs.overflow);
    const struct fnv_shader_pair_record *pair=missing.missing_pairs.rows;
    assert(pair->hits==2 && pair->reason==1 && !pair->fixed_bytes);
    assert(pair->attribute_state==1 && pair->attribute_count==2);
    assert(pair->attributes[0]==0x010000 && pair->attributes[1]==0x010005);
    assert(pair->vertex_bytes==catalog[stage_index[0]].size && pair->pixel_bytes==catalog[stage_index[1]].size);
    assert(pair->vertex_hash==fnv_shader_fingerprint(catalog[stage_index[0]].bytes,catalog[stage_index[0]].size));
    assert(pair->pixel_hash==fnv_shader_fingerprint(catalog[stage_index[1]].bytes,catalog[stage_index[1]].size));
    assert(!call(END,1,&device_at));
    assert(!fnv_vk_backend_link_program(gpu,catalog[stage_index[0]].bytes,catalog[stage_index[0]].size,catalog[stage_index[1]].bytes,catalog[stage_index[1]].size,map,2,1));
    assert(fnv_vk_backend_link_program(gpu,catalog[stage_index[0]].bytes,catalog[stage_index[0]].size,catalog[stage_index[1]].bytes,catalog[stage_index[1]].size,map,2,1)==FNV_VK_INVALID);memset(map,0xa5,sizeof(map));
    exercise_fixed_dispatch(vs,ps,decl);
    draw_constants(0,.75f);uint32_t draw_args[]={device_at,4,UINT32_MAX,1,4,2,2};
    assert(call(D_INDEXED,7,draw_args)==FNV_VK_INVALID);assert(!call(BEGIN,1,&device_at));
    draw_args[3]=2;assert(call(D_INDEXED,7,draw_args)==FNV_VK_INVALID && !stats().draws);draw_args[3]=1;
    /* Null samplers now have a native fallback; they are not invalid draws. */
    uint32_t no_texture[]={device_at,0,0};assert(!call(T_BIND,3,no_texture));assert(!call(T_BIND,3,texture_bind));
    stream[3]=17;assert(!call(B_STREAM,5,stream));assert(call(D_INDEXED,7,draw_args)==FNV_VK_INVALID);stream[3]=16;stream[4]=18;
    assert(!call(B_STREAM,5,stream));assert(call(D_INDEXED,7,draw_args)==FNV_VK_INVALID);stream[4]=16;assert(!call(B_STREAM,5,stream));
    struct fnv_vk_stats before=stats();
    for(unsigned fault=0;fault<9;++fault){
        if(fault==0)fail_memory=1;else if(fault==1)fail_map=1;else if(fault==2)fail_flush=1;else if(fault==3)fail_Sampler=1;
        else if(fault==4)fail_PipelineLayout=1;else if(fault==5)fail_DescriptorPool=1;else if(fault==6)fail_pipeline=1;else if(fault==7)fail_descriptors=1;else fail_Framebuffer=1;
        assert(call(D_INDEXED,7,draw_args)==FNV_VK_OOM);draw_build_faults++;
        assert(stats().allocated_bytes==before.allocated_bytes && stats().buffer_versions==before.buffer_versions && !stats().pending_frames && !stats().draws && !live_Pipeline);
    }
    assert(!call(D_INDEXED,7,draw_args));assert(stats().pending_frames==1 && stats().draws==1 && stats().buffer_versions==4);
    /* Change every mutable source before observing the draw's fence. */
    unsigned char zero_vb[96]={0},zero_ib[16]={0};upload_buffer(vb,0,96,0,zero_vb);upload_buffer(ib,0,16,0,zero_ib);
    assert(stats().buffer_versions==6);draw_texture(texture,second);assert(stats().texture_versions==1 && stats().pending_frames==1);
    draw_constants(1,.25f);assert(call(D_INDEXED,7,draw_args)==FNV_VK_INVALID); /* zero indices violate MinVertexIndex */
    draw_geometry(vb,ib,1);assert(call(D_INDEXED,7,draw_args)==FNV_VK_BUSY);
    assert(!call(END,1,&device_at) && stats().pending_frames==2);draw_pixels(0,first,0);
    assert(stats().buffer_versions==2 && stats().texture_versions==1);
    if(uniform_cache_requested){assert(!fnv_vk_backend_uniform_reuse(gpu,1));assert(!fnv_vk_backend_layout_reuse(gpu,1));assert(!fnv_vk_backend_sampler_reuse(gpu,1));}
    assert(!call(BEGIN,1,&device_at));assert(!call(D_INDEXED,7,draw_args));
    if(uniform_cache_requested){assert(fnv_vk_backend_uniform_reuse(gpu,0)==FNV_VK_BUSY);assert(fnv_vk_backend_layout_reuse(gpu,0)==FNV_VK_BUSY);assert(fnv_vk_backend_sampler_reuse(gpu,0)==FNV_VK_BUSY);}
    assert(!call(END,1,&device_at));draw_pixels(1,second,0);
    /* A farther blue draw must fail LEQUAL; disabling depth then reveals it. */
    draw_constants(2,.9f);draw_geometry(vb,ib,1);uint32_t sampler[]={device_at,0,1,2};assert(!call(D_SAMPLER_SET,4,sampler));
    uint32_t sampler_get[]={device_at,0,1,GUEST_OUT};assert(!call(D_SAMPLER_GET,4,sampler_get) && get(GUEST_OUT)==2);
    assert(!call(BEGIN,1,&device_at));
    if(uniform_cache_requested){struct fnv_vk_stats saved=stats();fail_flush=1;assert(call(D_INDEXED,7,draw_args)==FNV_VK_OOM);assert(stats().allocated_bytes==saved.allocated_bytes && stats().draws==saved.draws);
        fail_DescriptorPool=1;assert(call(D_INDEXED,7,draw_args)==FNV_VK_OOM);assert(stats().allocated_bytes==saved.allocated_bytes && stats().draws==saved.draws);}
    assert(!call(D_INDEXED,7,draw_args));assert(!call(END,1,&device_at));draw_pixels(1,second,0);
    /* A null depth surface suppresses depth without changing ZENABLE. */
    uint32_t depth_args[]={device_at,GUEST_OUT};assert(!call(GET_DS,2,depth_args));uint32_t saved_depth=get(GUEST_OUT);
    depth_args[1]=0;assert(!call(SET_DS,2,depth_args));
    uint32_t depth_state_get[]={device_at,7,GUEST_OUT};assert(!call(GET_RENDER,3,depth_state_get) && get(GUEST_OUT)==1);
    draw_mirrored=1;assert(!call(BEGIN,1,&device_at));assert(!call(D_INDEXED,7,draw_args));assert(!call(END,1,&device_at));draw_pixels(2,second,0);
    depth_args[1]=saved_depth;assert(!call(SET_DS,2,depth_args));release(saved_depth);
    assert(!call(GET_RENDER,3,depth_state_get) && get(GUEST_OUT)==1);
    render[1]=7;render[2]=0;assert(!call(SET_RENDER,3,render));
    /* Scissor applies to draw coverage, independently from viewport. */
    const uint32_t rect[4]={8,4,24,28};memcpy(ptr(DATA),rect,16);uint32_t scissor[]={device_at,DATA};assert(!call(SET_SCISSOR,2,scissor));
    render[1]=174;render[2]=1;assert(!call(SET_RENDER,3,render));draw_constants(0,.5f);
    assert(!call(BEGIN,1,&device_at));assert(!call(D_INDEXED,7,draw_args));assert(!call(END,1,&device_at));draw_pixels(0,second,1);
    exercise_render_states(vb,ib,texture,second);
    exercise_bc_draws(vb,ib,texture);exercise_up(vb,ib,texture);exercise_fvf(vb,ib,decl,texture);exercise_stateblocks(vb,ib,texture);
    /* Nonindexed triangle list consumes six vertices without an index binding. */
    render[2]=0;assert(!call(SET_RENDER,3,render));float vertices[24]={-1,1,1,0,1,1,2,0,1,-1,2,1,-1,1,1,0,1,-1,2,1,-1,-1,1,1};upload_buffer(vb,0,96,0,vertices);
    stream[3]=0;assert(!call(B_STREAM,5,stream));index[1]=0;assert(!call(B_INDICES,2,index));draw_constants(2,.5f);
    uint32_t primitive[]={device_at,4,0,2};assert(!call(BEGIN,1,&device_at));assert(!call(D_PRIMITIVE,4,primitive));assert(!call(END,1,&device_at));
    /* Public refs and all bindings can disappear while the draw is pending. */
    assert(!release(vs) && !release(ps) && !release(decl) && !release(vb) && !release(ib) && !release(texture));
    bind[1]=0;assert(!call(S_VS_BIND,2,bind) && !call(S_PS_BIND,2,bind) && !call(S_DECL_BIND,2,bind));
    stream[2]=stream[3]=stream[4]=0;assert(!call(B_STREAM,5,stream));assert(!call(T_BIND,3,no_texture));
    assert(stats().live_shaders==2 && stats().live_declarations==1 && stats().live_buffers==1 && stats().live_textures==1);
    draw_pixels(2,second,0);
    if(uniform_cache_requested){assert(stats().uniform_reuses>10 && stats().layout_hits>10 && stats().layout_creates<stats().draws);assert(!fnv_vk_backend_uniform_reuse(gpu,0));assert(!fnv_vk_backend_layout_reuse(gpu,0));assert(stats().sampler_hits>0);assert(!fnv_vk_backend_sampler_reuse(gpu,0));}
    assert(!stats().live_shaders && !stats().live_declarations && !stats().live_buffers && !stats().live_textures && !stats().buffer_versions && !stats().texture_versions);
    render[1]=7;render[2]=1;assert(!call(SET_RENDER,3,render));
}

static void exercise_draw_submit_faults(const struct fnv_vk_config *cfg,const char *vs_path,const char *ps_path){
    for(unsigned fault=0;fault<3;++fault){struct fnv_d3d9_shader_backend b;VkResult reason;
        assert(!fnv_vk_backend_create(proxy_gpa,cfg,&gpu,&reason));fnv_vk_backend_callbacks(gpu,&b);register_shader_files(vs_path,ps_path);
        unsigned stage_index[2]={0};for(unsigned i=0;i<catalog_count;++i)stage_index[((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff]=i;
        struct fnv_vk_vertex_input map[2]={{0,0,0},{5,0,1}};
        assert(!fnv_vk_backend_link_program(gpu,catalog[stage_index[0]].bytes,catalog[stage_index[0]].size,catalog[stage_index[1]].bytes,catalog[stage_index[1]].size,map,2,1));
        uintptr_t shaders[2],decl,vb,texture;
        for(unsigned i=0;i<2;++i){const struct fnv_d3d9_shader_code *code=catalog+stage_index[i];assert(!b.create(gpu,i,code->bytes,code->size,shaders+i));assert(!b.bind_shader(gpu,i,shaders[i]));}
        const unsigned char elements[24]={0,0,0,0,1,0,0,0,0,0,8,0,1,0,5,0,255,0,0,0,17,0,0,0};
        assert(!b.create_declaration(gpu,elements,3,&decl) && !b.bind_declaration(gpu,decl));
        float data[24]={0};assert(!b.create_buffer(gpu,FNV_D3D9_VB,96,0,0,1,&vb));assert(!b.update_buffer(gpu,vb,0,96,0,data));assert(!b.bind_stream(gpu,0,vb,0,16));
        const unsigned char pixels[16]={0};assert(!b.create_texture(gpu,2,2,1,0,21,1,&texture));assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,pixels));assert(!b.bind_texture(gpu,0,texture));
        unsigned char uploaded[16];assert(!fnv_vk_backend_read_texture(gpu,texture,0,uploaded,16,10000000000ull));uint32_t values[6]={1};assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,22,values,0));assert(!b.scene(gpu,1));
        if(fault==0)fail_end=1;else if(fault==1)fail_reset=1;else fail_submit=1;
        assert(b.draw(gpu,0,4,0,0,0,0,2)==(fault==2?FNV_VK_LOST:FNV_VK_OOM));assert(fnv_vk_backend_poll(gpu)==FNV_VK_LOST);
        assert(!stats().draws && live_Pipeline==1);b.release(gpu,shaders[0]);b.release(gpu,shaders[1]);b.release(gpu,decl);b.release(gpu,vb);b.release(gpu,texture);b.device_release(gpu);
        assert(!stats().device_references && stats().live_shaders==2 && stats().live_buffers==1 && stats().live_textures==1);
        if(fault==2){fail_idle=1;assert(fnv_vk_backend_destroy(gpu)==FNV_VK_LOST && live_Pipeline==1 && live_memory);}
        assert(!fnv_vk_backend_destroy(gpu));no_live_handles();
    }
}
