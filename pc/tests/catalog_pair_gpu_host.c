/* Explicit offline shader pair, original D3D9 versus our Vulkan provider.
 * Synthetic vertices/textures only; does not represent a game frame. */
#include <windows.h>
#include <d3d9.h>
#include "d3d9_vk_backend.h"
#include "d3d9_vk_prepared.h"
#include "catalog_pair_fixture.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
/* Test-only Vulkan dispatch proxy: remove the viewport correction without
 * changing guest vertices, shader programs or the Windows reference. */
static PFN_vkGetInstanceProcAddr real_gpa;
static PFN_vkGetDeviceProcAddr real_gdpa;
static PFN_vkCreateGraphicsPipelines real_pipelines;
static PFN_vkWaitForFences real_fence_wait;
static PFN_vkCreateShaderModule real_shader_module;
static PFN_vkDestroyShaderModule real_destroy_shader_module;
static unsigned live_shader_modules,fail_variant_module,skip_variant_locations;
static VKAPI_ATTR VkResult VKAPI_CALL test_shader_module(VkDevice d,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *allocator,VkShaderModule *out){
    if(fail_variant_module){fail_variant_module=0;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    VkResult r=real_shader_module(d,info,allocator,out);if(r==VK_SUCCESS)live_shader_modules++;return r;
}
static VKAPI_ATTR void VKAPI_CALL test_destroy_shader_module(VkDevice d,VkShaderModule module,const VkAllocationCallbacks *allocator){
    assert(module && live_shader_modules);live_shader_modules--;real_destroy_shader_module(d,module,allocator);
}
static unsigned skip_pixel_center;
static unsigned force_draw_timeout;
static VKAPI_ATTR VkResult VKAPI_CALL test_fence_wait(VkDevice d,uint32_t count,const VkFence *fences,VkBool32 all,uint64_t timeout){
    if(force_draw_timeout){force_draw_timeout=0;assert(!all && count==2 && timeout>0 && timeout<=10000000000ull);return VK_TIMEOUT;}
    return real_fence_wait(d,count,fences,all,timeout);
}
static VKAPI_ATTR VkResult VKAPI_CALL test_pipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out){
    if(!skip_pixel_center && !skip_variant_locations)return real_pipelines(d,cache,count,info,allocator,out);
    assert(count==1 && info->pViewportState && info->pViewportState->viewportCount==1);
    VkGraphicsPipelineCreateInfo next=*info;VkPipelineViewportStateCreateInfo state=*info->pViewportState;VkViewport viewport=*state.pViewports;
    if(skip_pixel_center){viewport.x-=.5f;viewport.y-=.5f;}state.pViewports=&viewport;next.pViewportState=&state;
    VkPipelineVertexInputStateCreateInfo vertex=*info->pVertexInputState;VkVertexInputAttributeDescription attributes[32];
    if(skip_variant_locations){assert(vertex.vertexAttributeDescriptionCount<=32);memcpy(attributes,vertex.pVertexAttributeDescriptions,vertex.vertexAttributeDescriptionCount*sizeof(*attributes));
        for(unsigned i=0;i<vertex.vertexAttributeDescriptionCount;i++)if(attributes[i].location>=2)attributes[i].location-=2;
        vertex.pVertexAttributeDescriptions=attributes;next.pVertexInputState=&vertex;}
    return real_pipelines(d,cache,count,&next,allocator,out);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL test_gdpa(VkDevice d,const char *name){
    PFN_vkVoidFunction p=real_gdpa(d,name);
    if(!strcmp(name,"vkCreateGraphicsPipelines")){assert(p);memcpy(&real_pipelines,&p,sizeof(p));return (PFN_vkVoidFunction)test_pipelines;}
    if(!strcmp(name,"vkWaitForFences")){assert(p);memcpy(&real_fence_wait,&p,sizeof(p));return (PFN_vkVoidFunction)test_fence_wait;}
    if(!strcmp(name,"vkCreateShaderModule")){assert(p);memcpy(&real_shader_module,&p,sizeof(p));return (PFN_vkVoidFunction)test_shader_module;}
    if(!strcmp(name,"vkDestroyShaderModule")){assert(p);memcpy(&real_destroy_shader_module,&p,sizeof(p));return (PFN_vkVoidFunction)test_destroy_shader_module;}
    return p;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL test_gpa(VkInstance instance,const char *name){
    PFN_vkVoidFunction p=real_gpa(instance,name);
    if(!strcmp(name,"vkGetDeviceProcAddr")){assert(p);memcpy(&real_gdpa,&p,sizeof(p));return (PFN_vkVoidFunction)test_gdpa;}
    return p;
}
#include "catalog_pair_oracle.h"
int main(int argc,char **argv){
    assert(argc==5 || argc==6);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));
    real_gpa=gp;gp=test_gpa;skip_pixel_center=argc==6 && !strcmp(argv[5],"missing-pixel-center");
    skip_variant_locations=argc==6 && !strcmp(argv[5],"wrong-variant-locations");
    struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
#if defined(FNV_CATALOG_TWO_TEXTURES) || defined(FNV_CATALOG_RENDER_TEXTURE)
    assert(fnv_vk_backend_draw_wait(gpu,UINT64_MAX)==FNV_VK_INVALID);
    assert(!fnv_vk_backend_draw_wait(gpu,argc==6 && !strcmp(argv[5],"nonblocking-ring")?0:10000000000ull));
#endif
#ifdef FNV_CATALOG_RENDER_TEXTURE
    uintptr_t render_texture=0,render_surface=0,back=0;
    struct fnv_vk_stats baseline;assert(!fnv_vk_backend_stats(gpu,&baseline));
    assert(!fnv_vk_backend_check_format(gpu,1,3,21) && !b.create_texture(gpu,32,32,1,1,21,0,&render_texture));
    struct fnv_vk_stats before_alias,after_alias;assert(!fnv_vk_backend_stats(gpu,&before_alias));
    assert(!b.texture_surface(gpu,render_texture,0,&render_surface) && !b.get_target(gpu,0,0,&back));
    uintptr_t again;assert(!b.texture_surface(gpu,render_texture,0,&again) && again==render_surface);b.release(gpu,again);
    assert(!fnv_vk_backend_stats(gpu,&after_alias) && before_alias.allocated_bytes==after_alias.allocated_bytes && before_alias.texture_versions==after_alias.texture_versions);
    uintptr_t invalid_surface=0x1234;assert(b.texture_surface(gpu,render_texture,1,&invalid_surface)==FNV_VK_INVALID && invalid_surface==0x1234);
    uintptr_t invalid_texture=0x1234;assert(b.create_texture(gpu,32,32,2,1,21,0,&invalid_texture)==FNV_VK_INVALID && invalid_texture==0x1234);
    assert(b.create_texture(gpu,32,32,1,1,21,1,&invalid_texture)==FNV_VK_INVALID && invalid_texture==0x1234);
    unsigned char invalid_upload[4]={0};assert(b.upload_texture(gpu,render_texture,0,0,0,1,1,0,4,invalid_upload)==FNV_VK_INVALID);
#endif
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];uintptr_t shader[2];
    struct fnv_vk_prepared_program prepared={.inputs=vertex_inputs,.input_count=2,.sampler_mask=FNV_CATALOG_SAMPLER_MASK};
    struct fnv_d3d9_shader_code catalog[2];
    for(unsigned i=0;i<2;i++){
        code[i]=load(argv[1+i],cb+i);spv[i]=load(argv[3+i],sb+i);struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        prepared.stages[i]=(struct fnv_vk_prepared_stage){code[i],spv[i],cb[i],sb[i],plan};catalog[i]=(struct fnv_d3d9_shader_code){code[i],cb[i]};
    }
    /* Missing PS in original package must not even install the VS. */
    assert(fnv_vk_prepare_program(gpu,&prepared,catalog,1)==FNV_VK_INVALID);
    struct fnv_vk_stats stats;assert(!fnv_vk_backend_stats(gpu,&stats) && !stats.shader_catalog_entries);
    assert(!fnv_vk_prepare_program(gpu,&prepared,catalog,2));
#ifdef FNV_CATALOG_SHARED_PROGRAM
    struct fnv_vk_prepared_program other=prepared;
    other.stages[1]=(struct fnv_vk_prepared_stage){shared_ps,shared_ps_spv,sizeof(shared_ps),sizeof(shared_ps_spv),shared_pixel_plan};
#ifdef FNV_CATALOG_PROGRAM_VARIANT
    other.stages[0].spirv=shared_vs_spv;other.stages[0].spirv_bytes=sizeof(shared_vs_spv);other.stages[0].plan=shared_vertex_plan;other.inputs=shared_vertex_inputs;
#endif
    struct fnv_d3d9_shader_code all[3]={catalog[0],catalog[1],{shared_ps,sizeof(shared_ps)}};
    assert(fnv_vk_prepare_program(gpu,&other,catalog,2)==FNV_VK_INVALID);
    assert(!fnv_vk_backend_stats(gpu,&stats) && stats.shader_catalog_entries==2);
    assert(!fnv_vk_prepare_program(gpu,&other,all,3));
    for(unsigned i=0;i<32;i++)assert(!fnv_vk_prepare_program(gpu,&prepared,all,3) && !fnv_vk_prepare_program(gpu,&other,all,3));
    assert(!fnv_vk_backend_stats(gpu,&stats) && stats.shader_catalog_entries==FNV_SHARED_STAGE_COUNT);
    struct fnv_vk_prepared_program conflict=prepared;
    unsigned char *changed=malloc(sb[0]);assert(changed);memcpy(changed,spv[0],sb[0]);changed[8]^=1;conflict.stages[0].spirv=changed;
    assert(fnv_vk_prepare_program(gpu,&conflict,all,3)==FNV_VK_INVALID);free(changed);
    struct fnv_uniform_entry wrong_entries[4];assert(vertex_plan.count==4);memcpy(wrong_entries,vertex_plan.entries,sizeof(wrong_entries));wrong_entries[0].offset+=4;conflict=prepared;conflict.stages[0].plan.entries=wrong_entries;
    assert(fnv_vk_prepare_program(gpu,&conflict,all,3)==FNV_VK_INVALID);
    conflict=prepared;conflict.sampler_mask=2;assert(fnv_vk_prepare_program(gpu,&conflict,all,3)==FNV_VK_INVALID);
    struct fnv_vk_vertex_input wrong_inputs[2]={vertex_inputs[0],vertex_inputs[1]};wrong_inputs[0].usage=1;conflict=prepared;conflict.inputs=wrong_inputs;
    assert(fnv_vk_prepare_program(gpu,&conflict,all,3)==FNV_VK_INVALID);
    assert(!fnv_vk_prepare_program(gpu,&prepared,all,3) && !fnv_vk_prepare_program(gpu,&other,all,3));
    assert(!fnv_vk_backend_stats(gpu,&stats) && stats.shader_catalog_entries==FNV_SHARED_STAGE_COUNT && !live_shader_modules);
    assert(fnv_vk_backend_register_shader(gpu,0,code[0],cb[0],spv[0],sb[0],&vertex_plan)==FNV_VK_INVALID);
    assert(fnv_vk_backend_link_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,2,1)==FNV_VK_INVALID);
    assert(!fnv_vk_backend_ensure_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,2,1));
    uintptr_t other_shader;assert(!b.create(gpu,1,shared_ps,sizeof(shared_ps),&other_shader));
#endif
    for(unsigned i=0;i<2;i++){assert(!b.create(gpu,i,code[i],cb[i],shader+i));assert(!b.bind_shader(gpu,i,shader[i]));}
    uintptr_t decl,texture,vb;
    assert(!b.create_declaration(gpu,elements,3,&decl) && !b.bind_declaration(gpu,decl));
#ifdef FNV_CATALOG_MIPS
    assert(!b.create_texture(gpu,8,8,4,0,21,1,&texture) && !b.bind_texture(gpu,0,texture));
    for(unsigned level=0;level<4;level++){
        unsigned char data[256];mip_texels(level,data);unsigned width=8u>>level;
        assert(!b.upload_texture(gpu,texture,level,0,0,width,width,0,width*4,data));
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
    }
#else
    assert(!b.create_texture(gpu,2,2,1,0,21,1,&texture) && !b.bind_texture(gpu,0,texture));
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
    uintptr_t secondary;assert(!b.create_texture(gpu,2,2,1,0,21,1,&secondary));
    uint32_t address[6]={3};assert(!b.state(gpu,FNV_D3D9_SAMPLER_STATE,1,1,address,0) && !b.state(gpu,FNV_D3D9_SAMPLER_STATE,1,2,address,0));
#endif
    assert(!b.create_buffer(gpu,FNV_D3D9_VB,144,0,0,1,&vb) && !b.bind_stream(gpu,0,vb,0,24));
    uint32_t state[6]={1};assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,22,state,0));state[0]=0;assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,7,state,0));
    const float xy[12]={-1,1,1,1,1,-1,-1,1,1,-1,-1,-1};unsigned checks=0;
    for(unsigned frame=0;frame<6;frame++){
        float vc[1024]={0},pc[896]={0};
        vc[0]=vc[5]=(frame&1)?.75f:1;vc[10]=vc[15]=1;vc[12]=(frame&2)?.125f:0;
        for(unsigned i=0;i<224;i++){pc[i*4]=.5f;pc[i*4+1]=.75f;pc[i*4+2]=(frame&1)?.25f:1;pc[i*4+3]=.625f;}
        float vertices[36];unsigned char texels[16]={31,83,211,193,173,47,109,127,71,229,53,89,251,139,17,241};
        for(unsigned i=0;i<16;i++)texels[i]^=(unsigned char)(frame*7);
        for(unsigned i=0;i<6;i++){
            vertices[i*6]=xy[i*2];vertices[i*6+1]=xy[i*2+1];vertices[i*6+2]=.5f;vertices[i*6+3]=1;
            vertices[i*6+4]=frame>=4?(xy[i*2]+1)*.5f:((frame&1)?.75f:.25f);
            vertices[i*6+5]=frame>=4?(1-xy[i*2+1])*.5f:((frame&2)?.75f:.25f);
#ifdef FNV_CATALOG_MIPS
            if(frame==2 || frame==4){float scale=frame==4?22.627417f:16;vertices[i*6+4]=(xy[i*2]+1)*.5f*scale;vertices[i*6+5]=(1-xy[i*2+1])*.5f*scale;}
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
            if(frame>=4){vertices[i*6+4]=xy[i*2]+1;vertices[i*6+5]=1-xy[i*2+1];}
#endif
        }
        const unsigned char *reference_ps=code[1];
#ifdef FNV_CATALOG_SHARED_PROGRAM
        if(frame&1)reference_ps=(const unsigned char*)shared_ps;
        assert(!b.bind_shader(gpu,1,(frame&1) && !(argc==6 && !strcmp(argv[5],"wrong-shared-program"))?other_shader:shader[1]));
#endif
        unsigned char expected[4096];oracle(frame,code[0],reference_ps,vertices,texels,vc,pc,expected);
#ifdef FNV_CATALOG_TWO_TEXTURES
        unsigned char second[16];secondary_texels(texels,second);assert(!b.upload_texture(gpu,secondary,0,0,0,2,2,0,8,second));
        int swapped=argc==6 && !strcmp(argv[5],"swapped-textures");assert(!b.bind_texture(gpu,0,swapped?secondary:texture) && !b.bind_texture(gpu,1,swapped?texture:secondary));
        address[0]=argc==6 && !strcmp(argv[5],"wrong-second-address")?1:3;assert(!b.state(gpu,FNV_D3D9_SAMPLER_STATE,1,1,address,0) && !b.state(gpu,FNV_D3D9_SAMPLER_STATE,1,2,address,0));
#endif
        if(argc==6 && !strcmp(argv[5],"wrong-uv"))for(unsigned i=0;i<6;i++)vertices[i*6+4]=.75f;
        if(argc==6 && !strcmp(argv[5],"wrong-texture"))for(unsigned i=0;i<4;i++){unsigned char t=texels[i*4];texels[i*4]=texels[i*4+2];texels[i*4+2]=t;}
        if(argc==6 && !strcmp(argv[5],"wrong-vs-constants"))memset(vc,0,sizeof(vc));
        if(argc==6 && !strcmp(argv[5],"wrong-ps-constants"))memset(pc,0,sizeof(pc));
#ifdef FNV_CATALOG_CONSTANTS
        uint32_t vbits[1024],pbits[896];memcpy(vbits,vc,sizeof(vbits));memcpy(pbits,pc,sizeof(pbits));
        assert(b.upload_constants(gpu,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,256,vbits));
        assert(b.upload_constants(gpu,FNV_D3D9_PIXEL,FNV_D3D9_FLOAT,0,224,pbits));
#endif
#ifdef FNV_CATALOG_MIPS
        state[0]=argc==6 && !strcmp(argv[5],"wrong-mip-clamp")?0:mip_clamp[frame];assert(!b.state(gpu,FNV_D3D9_SAMPLER_STATE,0,9,state,0));
        state[0]=argc==6 && !strcmp(argv[5],"wrong-mip-filter")?0:mip_filter[frame];assert(!b.state(gpu,FNV_D3D9_SAMPLER_STATE,0,7,state,0));
#else
        assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,texels));
#endif
        assert(!b.update_buffer(gpu,vb,0,144,0,vertices));
#ifdef FNV_CATALOG_RENDER_TEXTURE
        assert(!b.bind_texture(gpu,0,texture) && !b.set_target(gpu,0,0,render_surface) && !b.scene(gpu,1));
        assert(!b.bind_texture(gpu,0,render_texture));assert(b.draw(gpu,0,4,0,0,0,0,2)==FNV_VK_INVALID);assert(!b.bind_texture(gpu,0,texture));
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0) && !b.draw(gpu,0,4,0,0,0,0,2) && !b.scene(gpu,0));
        assert(!b.set_target(gpu,0,0,back) && !b.bind_texture(gpu,0,argc==6 && !strcmp(argv[5],"wrong-render-source")?texture:render_texture));
#endif
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));
        if(frame==5){uint32_t viewport[6]={4,2,24,28,0,0x3f800000};assert(!b.state(gpu,FNV_D3D9_VIEWPORT,0,0,viewport,0));}
        /* This pixel oracle drains transfers deliberately; not engine pacing. */
#ifndef FNV_CATALOG_RENDER_TEXTURE
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
#endif
        assert(!b.scene(gpu,1));
        force_draw_timeout=argc==6 && !strcmp(argv[5],"draw-timeout");
#ifdef FNV_CATALOG_PROGRAM_VARIANT
        if(!frame && argc==6 && !strcmp(argv[5],"variant-module-failure")){
            fail_variant_module=1;unsigned before=live_shader_modules;
            assert(b.draw(gpu,0,4,0,0,0,0,2)==FNV_VK_OOM && !fail_variant_module && live_shader_modules==before);
        }
#endif
        uint32_t draw_result=b.draw(gpu,0,4,0,0,0,0,2);
#ifdef FNV_CATALOG_TWO_TEXTURES
        if(argc==6 && !strcmp(argv[5],"draw-timeout")){
            assert(draw_result==FNV_VK_LOST && !fnv_vk_backend_stats(gpu,&stats) && stats.draws==0 && stats.draw_waits==1 && stats.pending_frames==2);
            puts("PASS draw watchdog retains pending GPU work");break;
        }
        if(argc==6 && !strcmp(argv[5],"nonblocking-ring")){
            assert(draw_result==FNV_VK_BUSY);assert(!fnv_vk_backend_wait(gpu,10000000000ull));draw_result=b.draw(gpu,0,4,0,0,0,0,2);
            assert(!fnv_vk_backend_stats(gpu,&stats) && stats.draw_waits==0);puts("PASS nonblocking ring retry");
        }
#endif
        assert(!draw_result && !b.scene(gpu,0));
#ifdef FNV_CATALOG_CONSTANTS
        /* A submitted draw must retain its snapshot after later API updates. */
        memset(vbits,0,sizeof(vbits));memset(pbits,0,sizeof(pbits));
        assert(b.upload_constants(gpu,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,256,vbits));
        assert(b.upload_constants(gpu,FNV_D3D9_PIXEL,FNV_D3D9_FLOAT,0,224,pbits));
#endif
        uintptr_t color;assert(!b.get_target(gpu,0,0,&color));
        unsigned char actual[4096];assert(!fnv_vk_backend_read_color(gpu,color,actual,sizeof(actual),10000000000ull));b.release(gpu,color);
        for(unsigned i=0;i<4096;i++)if(abs((int)actual[i]-(int)expected[i])>1){fprintf(stderr,"catalog pixel mismatch frame=%u byte=%u actual=%u expected=%u\n",frame,i,actual[i],expected[i]);return 2;}
        checks+=1024;
    }
#ifdef FNV_CATALOG_TWO_TEXTURES
    assert(!fnv_vk_backend_stats(gpu,&stats));
    if(argc==5){assert(stats.draw_waits>=6);printf("draw_waits=%llu\n",(unsigned long long)stats.draw_waits);assert(fnv_vk_backend_draw_wait(gpu,1)==FNV_VK_INVALID);}
    b.release(gpu,secondary);
#endif
#ifdef FNV_CATALOG_RENDER_TEXTURE
    /* Drop/recreate the surface while the sampled texture preserves pixels. */
    b.release(gpu,render_surface);assert(!b.texture_surface(gpu,render_texture,0,&render_surface));
    unsigned char retained[4096];assert(!fnv_vk_backend_read_color(gpu,render_surface,retained,sizeof(retained),10000000000ull));
    b.release(gpu,render_surface);b.release(gpu,render_texture);b.release(gpu,back);
#endif
#ifdef FNV_CATALOG_SHARED_PROGRAM
    b.release(gpu,other_shader);
#endif
    b.release(gpu,vb);b.release(gpu,decl);b.release(gpu,texture);for(unsigned i=0;i<2;i++){b.release(gpu,shader[i]);free(code[i]);free(spv[i]);}
#ifdef FNV_CATALOG_RENDER_TEXTURE
    assert(!b.bind_texture(gpu,0,0) && !b.bind_stream(gpu,0,0,0,0) && !b.bind_declaration(gpu,0));
    for(unsigned i=0;i<2;i++)assert(!b.bind_shader(gpu,i,0));
    assert(!fnv_vk_backend_wait(gpu,10000000000ull) && !fnv_vk_backend_stats(gpu,&stats));
    assert(stats.allocated_bytes==baseline.allocated_bytes && stats.live_surfaces==baseline.live_surfaces && !stats.live_textures && !stats.texture_versions && !stats.live_buffers && !stats.live_shaders && !stats.live_declarations && !stats.pending_frames);
#endif
    b.device_release(gpu);assert(!fnv_vk_backend_destroy(gpu) && !live_shader_modules);FreeLibrary(loader);printf("PASS catalog pair %u pixels\n",checks);return 0;
}
