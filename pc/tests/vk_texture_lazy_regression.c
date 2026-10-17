#ifdef _WIN32
#include <windows.h>
#endif
#include "d3d9_vk_backend.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
int main(void){
    PFN_vkGetInstanceProcAddr gpa;
#ifdef _WIN32
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");memcpy(&gpa,&raw,sizeof(gpa));assert(gpa);
#else
    gpa=vkGetInstanceProcAddr;
#endif
    struct fnv_vk_config cfg={32,32,16,2,4,64u<<20,0};struct fnv_vk_backend *d;VkResult why;
    assert(!fnv_vk_backend_create(gpa,&cfg,&d,&why));struct fnv_d3d9_shader_backend p;fnv_vk_backend_callbacks(d,&p);
    uintptr_t tex;assert(!p.create_texture(d,2,2,1,0,21,1,&tex));unsigned char source[16];memset(source,0x37,16);assert(!p.upload_texture(d,tex,0,0,0,2,2,0,8,source));
    struct fnv_vk_stats before,after;assert(!fnv_vk_backend_stats(d,&before) && !before.submissions);
    for(unsigned i=0;i<2;++i){assert(!p.clear(d,0,NULL,1,0xff000000,0,0));assert(!fnv_vk_backend_flush(d));}
    assert(!fnv_vk_backend_stats(d,&before) && before.pending_frames==2);
    unsigned char out[16];memset(out,0xa5,16);uint32_t full=fnv_vk_backend_read_texture(d,tex,0,out,16,5000000000ull);
    assert(!full && !memcmp(out,source,16));
    assert(!fnv_vk_backend_wait(d,5000000000ull));assert(!fnv_vk_backend_read_texture(d,tex,0,out,16,5000000000ull));assert(!memcmp(out,source,16));
    source[0]=0x77;assert(!p.upload_texture(d,tex,0,0,0,2,2,0,8,source));assert(!fnv_vk_backend_stats(d,&before));
    uint32_t invalid=fnv_vk_backend_read_texture(d,tex,99,out,16,5000000000ull);assert(invalid==FNV_VK_INVALID);assert(!fnv_vk_backend_stats(d,&after));
    assert(after.submissions==before.submissions && !after.pending_frames);
    assert(!fnv_vk_backend_wait(d,5000000000ull));p.release(d,tex);p.device_release(d);assert(!fnv_vk_backend_destroy(d));
#ifdef _WIN32
    FreeLibrary(loader);
#endif
    printf("{\"status\":\"PASS\",\"full_ring_read_waits_and_succeeds\":true,\"invalid_level_submits_no_gpu_work\":true,\"cleanup_completed\":true}\n");return 0;
}
