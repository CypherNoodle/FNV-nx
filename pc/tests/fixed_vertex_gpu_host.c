#include <windows.h>
#include "d3d9_vk_backend.h"
#include "d3d9_fixed_vertex.h"
#include "fixed_vertex_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
static unsigned char *load(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*bytes=(uint32_t)n;return p;
}
int main(int argc,char **argv){
    assert(argc==5 || argc==6);
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));assert(gp);
    struct fnv_vk_config config={32,32,16,2,32,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&config,&gpu,&failure));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];uintptr_t shader[2];
    for(unsigned i=0;i<2;i++){
        code[i]=load(argv[1+i],cb+i);spv[i]=load(argv[3+i],sb+i);
        struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        assert(!fnv_vk_backend_register_shader(gpu,i,code[i],cb[i],spv[i],sb[i],&plan));
        assert(!b.create(gpu,i,code[i],cb[i],shader+i) && !b.bind_shader(gpu,i,shader[i]));
    }
    assert(!fnv_vk_backend_link_program(gpu,code[0],cb[0],code[1],cb[1],vertex_inputs,2,0));
    const unsigned char elements[24]={0,0,0,0,2,0,0,0,0,0,12,0,1,0,5,0,255,0,0,0,17,0,0,0};uintptr_t decl;
    assert(!b.create_declaration(gpu,elements,3,&decl) && !b.bind_declaration(gpu,decl));
    float vertices[30]={-1,1,.5f,.25f,.75f,1,1,.5f,.25f,.75f,1,-1,.5f,.25f,.75f,
                        -1,1,.5f,.25f,.75f,1,-1,.5f,.25f,.75f,-1,-1,.5f,.25f,.75f};
    uintptr_t vb;assert(!b.create_buffer(gpu,FNV_D3D9_VB,sizeof(vertices),0,0,1,&vb));
    assert(!b.update_buffer(gpu,vb,0,sizeof(vertices),0,vertices) && !b.bind_stream(gpu,0,vb,0,20));
    uint32_t state[6]={1};assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,22,state,0));
    state[0]=0;assert(!b.state(gpu,FNV_D3D9_RENDER_STATE,0,7,state,0));
    struct fnv_transform_state bank;fnv_transform_init(&bank);unsigned checks=0;
    for(unsigned frame=0;frame<2;frame++){
        fnv_transform_init(&bank);
        if(frame){
            bank.matrices[10][0]=bank.matrices[10][5]=.5f;bank.matrices[10][12]=.25f;
            bank.matrices[0][12]=-.25f;bank.matrices[0][13]=.25f;bank.matrices[1][0]=.5f;
        }
        float columns[16];assert(!fnv_fixed_vertex_columns(&bank,columns));
        if(argc==6 && frame){
            if(!strcmp(argv[5],"wrong-transpose")){float tmp[16];memcpy(tmp,columns,64);for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++)columns[r*4+c]=tmp[c*4+r];}
            else if(!strcmp(argv[5],"wrong-order")){
                float pv[16],pvw[16];fnv_fixed_matrix_product(pv,bank.matrices[1],bank.matrices[0]);fnv_fixed_matrix_product(pvw,pv,bank.matrices[10]);
                for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++)columns[c*4+r]=pvw[r*4+c];
            }else assert(0);
        }
        uint32_t bits[16];memcpy(bits,columns,64);assert(b.upload_constants(gpu,FNV_D3D9_VERTEX,FNV_D3D9_FLOAT,0,4,bits));
        assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));
        assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
        assert(!b.scene(gpu,1));
        uint32_t draw_hr=b.draw(gpu,0,4,0,0,0,0,2);if(draw_hr)fprintf(stderr,"draw failed %08x\n",draw_hr);
        assert(!draw_hr && !b.scene(gpu,0));
        /* Mutating the original bank/temporary upload cannot alter this draw. */
        memset(&bank,0,sizeof(bank));memset(columns,0,64);memset(bits,0,64);
        uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char pixels[4096];
        assert(!fnv_vk_backend_read_color(gpu,color,pixels,sizeof(pixels),10000000000ull));b.release(gpu,color);
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++){
            int inside=!frame || (x>=12 && x<20 && y>=4 && y<20);
            const unsigned char expected[4]={0,inside?191:0,inside?64:0,255};
            for(unsigned c=0;c<4;c++)if(abs((int)pixels[(y*32+x)*4+c]-expected[c])>1){
                fprintf(stderr,"fixed vertex pixel mismatch frame=%u x=%u y=%u channel=%u actual=%u expected=%u\n",frame,x,y,c,pixels[(y*32+x)*4+c],expected[c]);return 1;
            }
            checks++;
        }
    }
    assert(!b.bind_stream(gpu,0,0,0,0) && !b.bind_declaration(gpu,0));
    for(unsigned i=0;i<2;i++){assert(!b.bind_shader(gpu,i,0));b.release(gpu,shader[i]);free(code[i]);free(spv[i]);}
    b.release(gpu,vb);b.release(gpu,decl);b.device_release(gpu);assert(!fnv_vk_backend_destroy(gpu));FreeLibrary(loader);
    printf("PASS fixed vertex pixels=%u; automatic fixed pipeline pending\n",checks);return 0;
}
