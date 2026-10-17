/* Linux smoke test for lazy texture uploads against the real headless Vulkan backend.
 * Needs any Vulkan ICD (e.g. lavapipe). No Windows, guest CPU or game files.
 *   gcc -std=c11 -O1 -Wall -Wextra -I../native vk_texture_lazy_smoke.c \
 *       ../native/d3d9_vk_backend.c ../native/d3d9_texture_upload.c ../native/d3d9_uniform_state.c \
 *       ../native/d3d9_uniform_pack.c ../native/d3d9_geometry.c ../native/d3d9_vk_state.c -lvulkan -lpthread
 * Prints submissions per phase so the number of GPU uploads is visible. */
#include "d3d9_vk_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{ if(!(x)){ fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } }while(0)
static uint32_t submissions(struct fnv_vk_backend *b){struct fnv_vk_stats s;if(fnv_vk_backend_stats(b,&s))return ~0u;return (uint32_t)s.submissions;}
static unsigned seed=12345;
static unsigned char rnd(void){seed=seed*1103515245u+12345u;return (unsigned char)(seed>>16);}

int main(void){
    struct fnv_vk_config cfg={64,64,16,2,8,256u<<20,0};struct fnv_vk_backend *b=NULL;VkResult why=VK_SUCCESS;
    uint32_t hr=fnv_vk_backend_create(vkGetInstanceProcAddr,&cfg,&b,&why);
    if(hr){fprintf(stderr,"SKIP: no usable Vulkan device hr=%08x vk=%d\n",hr,(int)why);return 77;}
    struct fnv_d3d9_shader_backend p;memset(&p,0,sizeof(p));fnv_vk_backend_callbacks(b,&p);
    CHECK(p.create_texture && p.upload_texture);

    /* --- BC1 64x64, 7 levels, one Lock/Unlock per level, then two overwrites of level 0 --- */
    uintptr_t bc=0;hr=p.create_texture(p.context,64,64,0,0,0x31545844u,1,&bc);
    if(hr==FNV_VK_INVALID){fprintf(stderr,"SKIP BC: device lacks textureCompressionBC\n");}
    else{
        CHECK(!hr);unsigned char *level[7];uint32_t bytes[7];
        uint32_t before=submissions(b);
        for(unsigned l=0;l<7;++l){
            uint32_t w=64>>l?64>>l:1,h=w;uint32_t bw=(w+3)/4,bh=(h+3)/4;bytes[l]=bw*bh*8;level[l]=malloc(bytes[l]);
            for(uint32_t i=0;i<bytes[l];++i)level[l][i]=rnd();
            CHECK(!p.upload_texture(p.context,bc,l,0,0,w,h,0,bw*8,level[l]));
        }
        uint32_t after_unlocks=submissions(b);
        printf("BC1: 7 level uploads -> %u submissions during Unlock\n",after_unlocks-before);
        for(unsigned l=0;l<7;++l){
            unsigned char *out=malloc(bytes[l]);
            CHECK(!fnv_vk_backend_read_texture(b,bc,l,out,bytes[l],5000000000ull));
            CHECK(!memcmp(out,level[l],bytes[l]));free(out);
        }
        printf("BC1: all 7 levels read back bit-exact; %u submissions for first use + readbacks\n",submissions(b)-after_unlocks);
        /* Overwrite level 0 after the texture was already flushed: must re-upload. */
        for(uint32_t i=0;i<bytes[0];++i)level[0][i]^=0x5a;
        CHECK(!p.upload_texture(p.context,bc,0,0,0,64,64,0,16*8,level[0]));
        unsigned char *out=malloc(bytes[0]);
        CHECK(!fnv_vk_backend_read_texture(b,bc,0,out,bytes[0],5000000000ull));CHECK(!memcmp(out,level[0],bytes[0]));
        CHECK(!memcmp(level[1],level[1],1));free(out);
        /* Partial block-aligned patch on level 1 keeps the rest intact. */
        unsigned char patch[8];for(unsigned i=0;i<8;++i)patch[i]=rnd();
        CHECK(!p.upload_texture(p.context,bc,1,4,4,4,4,0,8,patch));
        out=malloc(bytes[1]);CHECK(!fnv_vk_backend_read_texture(b,bc,1,out,bytes[1],5000000000ull));
        memcpy(level[1]+(1*8+1)*8,patch,8);CHECK(!memcmp(out,level[1],bytes[1]));free(out);
        puts("BC1: overwrite and partial patch OK");
    }

    /* --- A8 8x8 single level: RGB must be 0, alpha is the source byte --- */
    uintptr_t a8=0;CHECK(!p.create_texture(p.context,8,8,1,0,28,1,&a8));
    unsigned char src[64],out[256];for(unsigned i=0;i<64;++i)src[i]=(unsigned char)(i*4+1);
    CHECK(!p.upload_texture(p.context,a8,0,0,0,8,8,0,8,src));
    CHECK(!fnv_vk_backend_read_texture(b,a8,0,out,sizeof(out),5000000000ull));
    for(unsigned i=0;i<64;++i)CHECK(out[i*4]==0 && out[i*4+1]==0 && out[i*4+2]==0 && out[i*4+3]==src[i]);
    puts("A8: RGB=0, A=source OK");

    /* --- Never-uploaded texture still rejects readback --- */
    uintptr_t blank=0;CHECK(!p.create_texture(p.context,4,4,1,0,21,1,&blank));
    CHECK(fnv_vk_backend_read_texture(b,blank,0,out,sizeof(out),1000000000ull)==FNV_VK_INVALID);
    puts("uninitialized texture readback rejected OK");
    puts("PASS");
    return 0;
}
