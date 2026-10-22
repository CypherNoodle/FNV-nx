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
static unsigned skip_pixel_center;
static VKAPI_ATTR VkResult VKAPI_CALL test_pipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out){
    if(!skip_pixel_center)return real_pipelines(d,cache,count,info,allocator,out);
    assert(count==1 && info->pViewportState && info->pViewportState->viewportCount==1);
    VkGraphicsPipelineCreateInfo next=*info;VkPipelineViewportStateCreateInfo state=*info->pViewportState;VkViewport viewport=*state.pViewports;
    viewport.x-=.5f;viewport.y-=.5f;state.pViewports=&viewport;next.pViewportState=&state;
    return real_pipelines(d,cache,count,&next,allocator,out);
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL test_gdpa(VkDevice d,const char *name){
    PFN_vkVoidFunction p=real_gdpa(d,name);
    if(!strcmp(name,"vkCreateGraphicsPipelines")){assert(p);memcpy(&real_pipelines,&p,sizeof(p));return (PFN_vkVoidFunction)test_pipelines;}
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
    struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];uintptr_t shader[2];
    struct fnv_vk_prepared_program prepared={.inputs=vertex_inputs,.input_count=2,.sampler_mask=1};
    struct fnv_d3d9_shader_code catalog[2];
    for(unsigned i=0;i<2;i++){
        code[i]=load(argv[1+i],cb+i);spv[i]=load(argv[3+i],sb+i);struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        prepared.stages[i]=(struct fnv_vk_prepared_stage){code[i],spv[i],cb[i],sb[i],plan};catalog[i]=(struct fnv_d3d9_shader_code){code[i],cb[i]};
    }
    /* Missing PS in original package must not even install the VS. */
    assert(fnv_vk_prepare_program(gpu,&prepared,catalog,1)==FNV_VK_INVALID);
    struct fnv_vk_stats stats;assert(!fnv_vk_backend_stats(gpu,&stats) && !stats.shader_catalog_entries);
    assert(!fnv_vk_prepare_program(gpu,&prepared,catalog,2));
    for(unsigned i=0;i<2;i++){assert(!b.create(gpu,i,code[i],cb[i],shader+i));assert(!b.bind_shader(gpu,i,shader[i]));}
    uintptr_t decl,texture,vb;
    assert(!b.create_declaration(gpu,elements,3,&decl) && !b.bind_declaration(gpu,decl));
    assert(!b.create_texture(gpu,2,2,1,0,21,1,&texture) && !b.bind_texture(gpu,0,texture));
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
        }
        unsigned char expected[4096];oracle(frame,code[0],code[1],vertices,texels,vc,pc,expected);
        if(argc==6 && !strcmp(argv[5],"wrong-uv"))for(unsigned i=0;i<6;i++)vertices[i*6+4]=.75f;
        if(argc==6 && !strcmp(argv[5],"wrong-texture"))for(unsigned i=0;i<4;i++){unsigned char t=texels[i*4];texels[i*4]=texels[i*4+2];texels[i*4+2]=t;}
        if(argc==6 && !strcmp(argv[5],"wrong-vs-constants"))memset(vc,0,sizeof(vc));
        if(argc==6 && !strcmp(argv[5],"wrong-ps-constants"))memset(pc,0,sizeof(pc));
#ifdef FNV_CATALOG_CONSTANTS
        uint32_t vbits[1024],pbits[896];memcpy(vbits,vc,sizeof(vbits));memcpy(pbits,pc,sizeof(pbits));
        assert(b.upload_constants(gpu,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,256,vbits));
        assert(b.upload_constants(gpu,FNV_D3D9_PIXEL,FNV_D3D9_FLOAT,0,224,pbits));
#endif
        assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,texels) && !b.update_buffer(gpu,vb,0,144,0,vertices));
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));
        if(frame==5){uint32_t viewport[6]={4,2,24,28,0,0x3f800000};assert(!b.state(gpu,FNV_D3D9_VIEWPORT,0,0,viewport,0));}
        /* This pixel oracle drains transfers deliberately; not engine pacing. */
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull) && !b.scene(gpu,1));
        assert(!b.draw(gpu,0,4,0,0,0,0,2) && !b.scene(gpu,0));
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
    b.release(gpu,vb);b.release(gpu,decl);b.release(gpu,texture);for(unsigned i=0;i<2;i++){b.release(gpu,shader[i]);free(code[i]);free(spv[i]);}
    b.device_release(gpu);assert(!fnv_vk_backend_destroy(gpu));FreeLibrary(loader);printf("PASS catalog pair %u pixels\n",checks);return 0;
}
