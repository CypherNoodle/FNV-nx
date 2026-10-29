/* Linux smoke test for lazy texture uploads against the real headless Vulkan backend.
 * Needs any Vulkan ICD (e.g. lavapipe). No Windows, guest CPU or game files.
 *   gcc -std=c11 -O1 -Wall -Wextra -I../native vk_texture_lazy_smoke.c \
 *       ../native/d3d9_vk_backend.c ../native/d3d9_texture_upload.c ../native/d3d9_uniform_state.c \
 *       ../native/d3d9_uniform_pack.c ../native/d3d9_geometry.c ../native/d3d9_vk_state.c -lvulkan -lpthread
 * Prints submissions per phase so the number of GPU uploads is visible. */
#include "d3d9_vk_backend.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{ if(!(x)){ fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } }while(0)
static uint32_t submissions(struct fnv_vk_backend *b){struct fnv_vk_stats s;if(fnv_vk_backend_stats(b,&s))return ~0u;return (uint32_t)s.submissions;}
static unsigned seed=12345;
static unsigned char rnd(void){seed=seed*1103515245u+12345u;return (unsigned char)(seed>>16);}

#include "vk_cube_texture_fixture.h"

int main(void){
    struct fnv_vk_config cfg={64,64,16,2,8,256u<<20,0};struct fnv_vk_backend *b=NULL;VkResult why=VK_SUCCESS;
    PFN_vkGetInstanceProcAddr gpa;
#ifdef _WIN32
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(loader);FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");memcpy(&gpa,&raw,sizeof(gpa));
#else
    gpa=vkGetInstanceProcAddr;
#endif
    uint32_t hr=fnv_vk_backend_create(gpa,&cfg,&b,&why);
    if(hr){fprintf(stderr,"SKIP: no usable Vulkan device hr=%08x vk=%d\n",hr,(int)why);return 77;}
    struct fnv_d3d9_shader_backend p;memset(&p,0,sizeof(p));fnv_vk_backend_callbacks(b,&p);
    CHECK(p.create_texture && p.upload_texture);
    CHECK(!fnv_vk_backend_gpu_timing(b,16));
    /* R32F must preserve float bits and support a real render attachment. */
    {
    uintptr_t floats=0;CHECK(!p.create_texture(b,3,2,1,0,114,1,&floats));
    const uint32_t bits[6]={0,0x3f800000,0xbf800000,0x3e800000,0x00800000,0x7f7fffff};uint32_t copied[6];
    CHECK(!p.upload_texture(b,floats,0,0,0,3,2,0,12,bits));
    CHECK(!fnv_vk_backend_read_texture(b,floats,0,copied,sizeof(copied),5000000000ull));CHECK(!memcmp(bits,copied,sizeof(bits)));
    uint32_t patch=0x3f000000;CHECK(!p.upload_texture(b,floats,0,1,1,1,1,0,4,&patch));
    CHECK(!fnv_vk_backend_read_texture(b,floats,0,copied,sizeof(copied),5000000000ull));
    CHECK(copied[4]==patch && copied[3]==bits[3] && copied[5]==bits[5]);p.release(b,floats);
    uintptr_t rt_texture=0,rt_surface=0,old_target=0;
    CHECK(!p.create_texture(b,4,4,1,1,114,0,&rt_texture));CHECK(!p.texture_surface(b,rt_texture,0,&rt_surface));
    uint32_t desc[8];CHECK(!p.surface_desc(b,rt_surface,desc) && desc[0]==114);
    CHECK(!p.get_target(b,0,0,&old_target));CHECK(!p.set_target(b,0,0,rt_surface));
    CHECK(!p.clear(b,0,NULL,1,0xff400000,0,0));CHECK(!fnv_vk_backend_flush(b));CHECK(!fnv_vk_backend_wait(b,5000000000ull));
    float cleared[16];CHECK(!fnv_vk_backend_read_texture(b,rt_texture,0,cleared,sizeof(cleared),5000000000ull));
    for(unsigned i=0;i<16;i++)CHECK(cleared[i]==64.0f/255.0f);
    CHECK(!p.set_target(b,0,0,old_target));p.release(b,old_target);p.release(b,rt_surface);p.release(b,rt_texture);
    CHECK(!p.create_texture(b,4,4,1,1,22,0,&rt_texture));CHECK(!p.texture_surface(b,rt_texture,0,&rt_surface));
    CHECK(!p.surface_desc(b,rt_surface,desc) && desc[0]==22);
    CHECK(!p.get_target(b,0,0,&old_target));CHECK(!p.set_target(b,0,0,rt_surface));
    CHECK(!p.clear(b,0,NULL,1,0x00123456,0,0));CHECK(!fnv_vk_backend_flush(b));CHECK(!fnv_vk_backend_wait(b,5000000000ull));
    uint32_t opaque[16];CHECK(!fnv_vk_backend_read_color(b,rt_surface,opaque,sizeof(opaque),5000000000ull));
    for(unsigned i=0;i<16;i++)CHECK((opaque[i]&0xffffff)==0x123456);
    CHECK(!p.set_target(b,0,0,old_target));p.release(b,old_target);p.release(b,rt_surface);p.release(b,rt_texture);
    puts("X8R8G8B8: render target descriptor and GPU clear/readback PASS");
    puts("R32F: float bits, partial update, target descriptor and GPU clear PASS");
    }

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
        uint32_t after_unlocks=submissions(b);CHECK(after_unlocks==before);
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
        free(out);out=malloc(bytes[1]);CHECK(out);CHECK(!fnv_vk_backend_read_texture(b,bc,1,out,bytes[1],5000000000ull));CHECK(!memcmp(out,level[1],bytes[1]));free(out);
        /* Partial block-aligned patch on level 1 keeps the rest intact. */
        unsigned char patch[8];for(unsigned i=0;i<8;++i)patch[i]=rnd();
        CHECK(!p.upload_texture(p.context,bc,1,4,4,4,4,0,8,patch));
        out=malloc(bytes[1]);CHECK(!fnv_vk_backend_read_texture(b,bc,1,out,bytes[1],5000000000ull));
        memcpy(level[1]+(1*8+1)*8,patch,8);CHECK(!memcmp(out,level[1],bytes[1]));free(out);
        puts("BC1: overwrite and partial patch OK");
        for(unsigned l=0;l<7;++l)free(level[l]);p.release(p.context,bc);
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
    /* Packed BGR24 with odd row pitch expands to RGBA8 without touching neighbors. */
    uintptr_t rgb24=0;CHECK(!fnv_vk_backend_check_format(b,0,3,20));CHECK(!p.create_texture(p.context,3,2,1,0,20,1,&rgb24));
    unsigned char packed[20]={1,2,3,4,5,6,7,8,9,0xcc,0xdd,10,11,12,13,14,15,16,17,18};
    CHECK(!p.upload_texture(p.context,rgb24,0,0,0,3,2,0,11,packed));unsigned char expanded[24];CHECK(!fnv_vk_backend_read_texture(b,rgb24,0,expanded,sizeof(expanded),1000000000ull));
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<3;x++){unsigned at=y*12+x*4,src=y*11+x*3;CHECK(expanded[at]==packed[src+2] && expanded[at+1]==packed[src+1] && expanded[at+2]==packed[src] && expanded[at+3]==255);}
    unsigned char patch[3]={91,92,93};CHECK(!p.upload_texture(p.context,rgb24,0,1,1,1,1,0,3,patch));unsigned char partial[24];CHECK(!fnv_vk_backend_read_texture(b,rgb24,0,partial,sizeof(partial),1000000000ull));
    CHECK(partial[16]==93 && partial[17]==92 && partial[18]==91 && partial[19]==255);CHECK(!memcmp(partial,expanded,16) && !memcmp(partial+20,expanded+20,4));p.release(p.context,rgb24);
    puts("BGR24 odd pitch, opaque alpha and partial upload GPU readback OK");
    CHECK(!cube_textures(b,&p));
    /* Managed mip surfaces are logical aliases; they must not allocate GPU images. */
    uintptr_t managed=0,aliases[4];CHECK(!p.create_texture(p.context,8,8,0,0,21,1,&managed));
    struct fnv_vk_stats prior;CHECK(!fnv_vk_backend_stats(b,&prior));
    for(unsigned l=0;l<4;l++) {
        uintptr_t again=0;CHECK(!p.texture_surface(p.context,managed,l,&aliases[l]));
        CHECK(!p.texture_surface(p.context,managed,l,&again) && again==aliases[l]);p.release(p.context,again);
        uint32_t desc[8];CHECK(!p.surface_desc(p.context,aliases[l],desc));
        CHECK(desc[0]==21 && desc[1]==1 && desc[2]==0 && desc[3]==1 && desc[6]==(8u>>l) && desc[7]==(8u>>l));
        CHECK(p.set_target(p.context,0,0,aliases[l])==FNV_VK_INVALID);
    }
    uintptr_t invalid=0;CHECK(p.texture_surface(p.context,managed,4,&invalid)==FNV_VK_INVALID);
    struct fnv_vk_stats after;CHECK(!fnv_vk_backend_stats(b,&after) && after.allocated_bytes==prior.allocated_bytes);
    p.release(p.context,managed);
    for(unsigned l=0;l<4;l++){uint32_t desc[8];CHECK(!p.surface_desc(p.context,aliases[l],desc));p.release(p.context,aliases[l]);}
    puts("managed mip surface identity, dimensions, lifetime and no GPU allocation OK");
    p.release(p.context,a8);p.release(p.context,blank);p.device_release(p.context);CHECK(!fnv_vk_backend_wait(b,5000000000ull));
    struct fnv_vk_stats final;CHECK(!fnv_vk_backend_stats(b,&final));CHECK(!final.device_references && !final.live_textures && !final.allocated_bytes);
    CHECK(final.gpu_samples>0 && final.gpu_sample_ns>0 && !final.gpu_query_failures);
    printf("GPU timestamps: samples=%llu ns=%llu stride=%u PASS\n",(unsigned long long)final.gpu_samples,(unsigned long long)final.gpu_sample_ns,final.gpu_sample_stride);
    CHECK(!fnv_vk_backend_destroy(b));
#ifdef _WIN32
    FreeLibrary(loader);
#endif
    puts("PASS");
    return 0;
}

