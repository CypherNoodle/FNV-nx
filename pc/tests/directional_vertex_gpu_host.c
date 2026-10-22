#include <windows.h>
#include <d3d9.h>
#include "d3d9_vk_backend.h"
#include "d3d9_directional_vertex.h"
#include "directional_vertex_fixture.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
static unsigned char *load(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*bytes=(uint32_t)n;return p;
}
#include "directional_vertex_oracle.h"
#ifdef FNV_DIRECTIONAL_TEXTURED
#define INPUT_COUNT 3
#define SAMPLER_MASK 1
#else
#define INPUT_COUNT 2
#define SAMPLER_MASK 0
#endif
int main(int argc,char **argv){
    assert(argc==5 || argc==6);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));
    struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];uintptr_t shader[2];
    for(unsigned i=0;i<2;i++){code[i]=load(argv[1+i],cb+i);spv[i]=load(argv[3+i],sb+i);struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        assert(!fnv_vk_backend_register_shader(gpu,i,code[i],cb[i],spv[i],sb[i],&plan));assert(!b.create(gpu,i,code[i],cb[i],shader+i));assert(!b.bind_shader(gpu,i,shader[i]));}
#ifdef FNV_FIXED_AUTOMATIC
#ifdef FNV_DIRECTIONAL_TEXTURED
#ifdef FNV_FIXED_ALPHA_TEXTURE
#define FIXED_PIXEL_MODE 2
#else
#define FIXED_PIXEL_MODE 1
#endif
#else
#define FIXED_PIXEL_MODE 0
#endif
    assert(!fnv_vk_backend_fixed_pixel(gpu,FIXED_PIXEL_MODE,shader[1]));
#endif
    assert(!fnv_vk_backend_fixed_directional(gpu,shader[0]));assert(fnv_vk_backend_fixed_vertex(gpu,shader[0])==FNV_VK_INVALID);
    assert(!fnv_vk_backend_link_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,INPUT_COUNT,SAMPLER_MASK));
#ifdef FNV_DIRECTIONAL_TEXTURED
    const unsigned char elements[32]={0,0,0,0,2,0,0,0,0,0,12,0,2,0,3,0,0,0,24,0,1,0,5,0,255,0,0,0,17,0,0,0};
    uintptr_t texture;assert(!b.create_texture(gpu,2,2,1,0,21,1,&texture) && !b.bind_texture(gpu,0,texture));
#else
    const unsigned char elements[24]={0,0,0,0,2,0,0,0,0,0,12,0,2,0,3,0,255,0,0,0,17,0,0,0};
#endif
    uintptr_t decl;
    assert(!b.create_declaration(gpu,elements,sizeof(elements)/8,&decl) && !b.bind_declaration(gpu,decl));
    const float positions[18]={-1,1,.5f,1,1,.5f,1,-1,.5f,-1,1,.5f,1,-1,.5f,-1,-1,.5f};float vertices[48]={0};
    uintptr_t vb;assert(!b.create_buffer(gpu,FNV_D3D9_VB,sizeof(vertices),0,0,1,&vb));assert(!b.bind_stream(gpu,0,vb,0,32));
    uint32_t state[6]={1};assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,22,state,0));state[0]=0;
    assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,7,state,0) && !b.state(gpu,FNV_D3D9_RENDER_STATE,0,141,state,0));
    unsigned checks=0;
    for(unsigned frame=0;frame<7;frame++){
        if(frame==5){
            assert(!b.bind_shader(gpu,0,0));b.release(gpu,shader[0]);shader[0]=0;
#ifdef FNV_FIXED_AUTOMATIC
            assert(!b.bind_shader(gpu,1,0));b.release(gpu,shader[1]);shader[1]=0;
#endif
            assert(!fnv_vk_backend_reset(gpu,10000000000ull));
            assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,141,state,1) && state[0]==1);
            assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,143,state,1) && state[0]==0);
            assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,139,state,1) && state[0]==0);
            assert(!b.bind_shader(gpu,1,shader[1]) && !b.bind_declaration(gpu,decl) && !b.bind_stream(gpu,0,vb,0,32));
            state[0]=1;assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,22,state,0));state[0]=0;
            assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,7,state,0) && !b.state(gpu,FNV_D3D9_RENDER_STATE,0,141,state,0));
        }
#ifdef FNV_DIRECTIONAL_TEXTURED
        assert(!b.bind_texture(gpu,0,texture));
        assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,directional_texels));
        unsigned char texture_copy[16];assert(!fnv_vk_backend_read_texture(gpu,texture,0,texture_copy,16,10000000000ull));
#endif
#ifdef FNV_FIXED_AUTOMATIC
        assert(!b.bind_shader(gpu,1,frame==4?shader[1]:0));
        struct fnv_d3d9_state_value stage_transaction[2]={{.kind=FNV_D3D9_TEXTURE_STAGE_STATE,.sampler=0,.name=1,.values={99}},
            {.kind=FNV_D3D9_TEXTURE_STAGE_STATE,.sampler=2,.name=1}};
        uint32_t previous[6]={0};assert(!b.state(gpu,FNV_D3D9_TEXTURE_STAGE_STATE,0,1,previous,1));
        assert(b.state_batch(gpu,stage_transaction,2,0)==FNV_VK_INVALID);
        assert(!b.state(gpu,FNV_D3D9_TEXTURE_STAGE_STATE,0,1,state,1) && state[0]==previous[0]);
        const uint32_t stages[6]={
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
        for(unsigned i=0;i<6;i++){state[0]=stages[i];assert(!b.state(gpu,FNV_D3D9_TEXTURE_STAGE_STATE,0,i+1,state,0));}
#endif
        struct fnv_transform_state bank;fnv_transform_init(&bank);float material[17]={.5f,.25f,.75f,.6f},light[26]={0};
        uint32_t words[26],mat[17],ambient=0,on=frame!=3;
        light[1]=light[2]=light[3]=1;light[18]=frame==1?-1:1;
        if(frame==1 || frame==2){material[12]=.0625f;material[13]=.125f;}
        if(frame==2 || frame==6)bank.matrices[10][0]=2;
        if(frame==3){material[4]=.5f;material[5]=.25f;material[6]=.125f;material[12]=.0625f;material[14]=.125f;ambient=0x204060;}
        memcpy(words,light,104);words[0]=3;memcpy(mat,material,68);struct fnv_light_bank lights={0};assert(!fnv_light_change(&lights,NULL,0,words,&on));
        for(unsigned i=0;i<6;i++){memcpy(vertices+i*8,positions+i*3,12);vertices[i*8+3]=(frame==2 || frame==6)?1:0;vertices[i*8+4]=0;vertices[i*8+5]=-1;}
#ifdef FNV_DIRECTIONAL_TEXTURED
        directional_uv(frame,vertices);
#endif
        float constants[40];assert(!fnv_fixed_directional_columns(&bank,mat,&lights,ambient,frame!=6,constants));
        if(!frame){
            float rejected[40],saved[40];memset(rejected,0xa5,sizeof(rejected));memcpy(saved,rejected,sizeof(saved));
            uint32_t yes=1,no=0,bad[26];memcpy(bad,words,104);
            assert(!fnv_light_change(&lights,NULL,1,bad,&yes));
            assert(fnv_fixed_directional_columns(&bank,mat,&lights,0,1,rejected)==0x8876086au && !memcmp(rejected,saved,sizeof(saved)));
            assert(!fnv_light_change(&lights,NULL,1,NULL,&no));bad[0]=1;assert(!fnv_light_change(&lights,NULL,0,bad,NULL));
            assert(fnv_fixed_directional_columns(&bank,mat,&lights,0,1,rejected)==0x8876086au && !memcmp(rejected,saved,sizeof(saved)));
            bad[0]=3;bad[16]=bad[17]=bad[18]=0;assert(!fnv_light_change(&lights,NULL,0,bad,NULL));
            assert(fnv_fixed_directional_columns(&bank,mat,&lights,0,1,rejected)==FNV_VK_INVALID && !memcmp(rejected,saved,sizeof(saved)));
        }
        fnv_light_destroy(&lights);
        unsigned char expected[4096];oracle(frame,vertices,&bank,mat,words,ambient,expected);
        if(argc==6 && frame==2){
            if(!strcmp(argv[5],"wrong-normal"))constants[16]=2;else if(!strcmp(argv[5],"wrong-direction"))constants[30]=-constants[30];else if(strcmp(argv[5],"wrong-uv") && strcmp(argv[5],"wrong-texture-alpha"))assert(0);
        }
#ifdef FNV_DIRECTIONAL_TEXTURED
        if(argc==6 && !strcmp(argv[5],"wrong-uv"))directional_uv(frame^1,vertices);
        if(argc==6 && !strcmp(argv[5],"wrong-texture-alpha")){
            unsigned char bad[16];memcpy(bad,directional_texels,16);for(unsigned i=0;i<4;i++)bad[i*4+3]=255;
            assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,bad));
            assert(!fnv_vk_backend_read_texture(gpu,texture,0,texture_copy,16,10000000000ull));
        }
#endif
        if(!frame){uint32_t bits[40];memcpy(bits,constants,160);assert(b.upload_constants(gpu,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,10,bits));}
        assert(!b.bind_shader(gpu,0,frame==4?shader[0]:0));
        assert(!b.update_buffer(gpu,vb,0,sizeof(vertices),0,vertices));assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull) && !b.scene(gpu,1));
        uint32_t hr=frame==4?b.draw(gpu,0,4,0,0,0,0,2):b.draw_fixed_directional(gpu,0,4,0,0,0,0,2,constants);assert(!hr && !b.scene(gpu,0));
#ifdef FNV_DIRECTIONAL_TEXTURED
        unsigned char poison[16]={0};assert(!b.upload_texture(gpu,texture,0,0,0,2,2,0,8,poison));
#endif
        memset(constants,0,160);uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char actual[4096];
        assert(!fnv_vk_backend_read_color(gpu,color,actual,sizeof(actual),10000000000ull));b.release(gpu,color);
        for(unsigned i=0;i<4096;i++){if(abs((int)actual[i]-expected[i])>1){fprintf(stderr,"directional pixel mismatch frame=%u byte=%u actual=%u expected=%u\n",frame,i,actual[i],expected[i]);return 1;}if(i%4==0)checks++;}
    }
    assert(!b.bind_stream(gpu,0,0,0,0) && !b.bind_declaration(gpu,0));for(unsigned i=0;i<2;i++){assert(!b.bind_shader(gpu,i,0));if(shader[i])b.release(gpu,shader[i]);free(code[i]);free(spv[i]);}
#ifdef FNV_DIRECTIONAL_TEXTURED
    assert(!b.bind_texture(gpu,0,0));b.release(gpu,texture);
#endif
    b.release(gpu,vb);b.release(gpu,decl);b.device_release(gpu);assert(!fnv_vk_backend_destroy(gpu));FreeLibrary(loader);
    printf("PASS directional vertex pixels=%u compared with Windows HAL; programmable constants preserved\n",checks);return 0;
}
