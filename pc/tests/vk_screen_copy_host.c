/* Explicit copy-pair fixture: XYZ/UV quad, c0 geometry offset, c1 UV offset. */
#include <windows.h>
#include "d3d9_vk_backend.h"
#include FNV_SCREEN_COPY_HEADER
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){
    int wrong_uv=argc==2 && !strcmp(argv[1],"--wrong-uv");CHECK(argc==1 || wrong_uv);
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(loader);
    FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gpa;memcpy(&gpa,&raw,sizeof(gpa));
    struct fnv_vk_config cfg={32,32,4096,3,32,64u<<20,0};struct fnv_vk_backend *d=NULL;VkResult why;
    CHECK(!fnv_vk_backend_create(gpa,&cfg,&d,&why));struct fnv_d3d9_shader_backend p;fnv_vk_backend_callbacks(d,&p);
    const struct fnv_vk_prepared_program *program=&fnv_startup_program;
    CHECK(program->input_count==2 && program->sampler_mask==1 && program->stages[0].plan.bytes==32 && !program->stages[1].plan.bytes);
    uintptr_t shader[2];
    for(unsigned i=0;i<2;i++){const struct fnv_vk_prepared_stage *s=program->stages+i;
        CHECK(!fnv_vk_backend_register_shader(d,(enum fnv_d3d9_stage)i,s->code,s->code_bytes,s->spirv,s->spirv_bytes,&s->plan));
        CHECK(!p.create(p.context,(enum fnv_d3d9_stage)i,s->code,s->code_bytes,shader+i));CHECK(!p.bind_shader(p.context,(enum fnv_d3d9_stage)i,shader[i]));}
    CHECK(!fnv_vk_backend_link_program(d,program->stages[0].code,program->stages[0].code_bytes,program->stages[1].code,program->stages[1].code_bytes,program->inputs,2,1));
    const unsigned char decl[]={0,0,0,0,2,0,0,0,0,0,12,0,1,0,5,0,255,0,0,0,17,0,0,0};uintptr_t declaration,vb,texture;
    CHECK(!p.create_declaration(p.context,decl,3,&declaration));CHECK(!p.bind_declaration(p.context,declaration));
    const float vertices[]={-1,1,0,0,0,-1,-1,0,0,1,1,-1,0,1,1,1,1,0,1,0};
    CHECK(!p.create_buffer(p.context,FNV_D3D9_VB,sizeof vertices,0,0,1,&vb));CHECK(!p.update_buffer(p.context,vb,0,sizeof vertices,0,vertices));CHECK(!p.bind_stream(p.context,0,vb,0,20));
    const uint32_t colors[4]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffff00u};
    CHECK(!p.create_texture(p.context,2,2,1,0,21,1,&texture));CHECK(!p.upload_texture(p.context,texture,0,0,0,2,2,0,8,colors));CHECK(!p.bind_texture(p.context,0,texture));
    const float constants[8]={.5f/32,.5f/32,0,0,wrong_uv?.5f:0,0,0,0};CHECK(p.upload_constants(p.context,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,2,(const uint32_t*)constants));
    uint32_t values[6]={0};CHECK(!p.state(p.context,FNV_D3D9_RENDER_STATE,0,7,values,0));values[0]=1;CHECK(!p.state(p.context,FNV_D3D9_RENDER_STATE,0,22,values,0));
    CHECK(!p.state(p.context,FNV_D3D9_SAMPLER_STATE,0,5,values,0));CHECK(!p.state(p.context,FNV_D3D9_SAMPLER_STATE,0,6,values,0));values[0]=0;CHECK(!p.state(p.context,FNV_D3D9_SAMPLER_STATE,0,7,values,0));
    CHECK(!p.scene(p.context,1));CHECK(!p.draw(p.context,0,6,0,0,0,0,2));CHECK(!p.scene(p.context,0));
    uintptr_t back;CHECK(!p.get_target(p.context,0,0,&back));uint32_t pixels[1024];CHECK(!fnv_vk_backend_read_color(d,back,pixels,sizeof pixels,1000000000ull));
    unsigned mismatches=0;for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){uint32_t expected=colors[(y>=16)*2+(x>=16)];if(pixels[y*32+x]!=expected){if(mismatches<4)fprintf(stderr,"pixel %u,%u actual=%08x expected=%08x\n",x,y,pixels[y*32+x],expected);mismatches++;}}
    CHECK(!mismatches);p.release(p.context,back);p.release(p.context,texture);p.release(p.context,vb);p.release(p.context,declaration);for(unsigned i=0;i<2;i++)p.release(p.context,shader[i]);p.device_release(p.context);
    CHECK(!fnv_vk_backend_wait(d,1000000000ull));CHECK(!fnv_vk_backend_destroy(d));FreeLibrary(loader);
    puts("{\"status\":\"PASS\",\"quadrant_pixels\":1024,\"gpu_rendered\":true,\"switch_tested\":false}");return 0;
}
