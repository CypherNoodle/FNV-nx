/* Own generated shaders only. Check batch preflight, partial failure and reuse. */
#include <windows.h>
#include "d3d9_vk_prepared.h"
#include "bundle.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
#include <string.h>
int main(void){
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&raw,sizeof(gp));
    struct fnv_d3d9_shader_code catalog[4];size_t count=0;
    assert(FNV_STARTUP_PROGRAM_COUNT==2);
    for(unsigned i=0;i<FNV_STARTUP_PROGRAM_COUNT;i++)for(unsigned stage=0;stage<2;stage++){
        const struct fnv_vk_prepared_stage *s=fnv_startup_programs[i]->stages+stage;size_t j=0;
        while(j<count && (catalog[j].size!=s->code_bytes || memcmp(catalog[j].bytes,s->code,s->code_bytes)))j++;
        if(j==count)catalog[count++]=(struct fnv_d3d9_shader_code){s->code,s->code_bytes};
    }
    assert(count==3);
    for(unsigned mode=0;mode<3;mode++){
        struct fnv_vk_config cfg={32,32,16,2,8,8u<<20,0};struct fnv_vk_backend *gpu;VkResult reason;
        assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&reason));struct fnv_vk_stats before,after;
        assert(!fnv_vk_backend_stats(gpu,&before));size_t failed=123;
        assert(fnv_vk_prepare_programs(gpu,NULL,2,catalog,count,&failed)==FNV_VK_INVALID && failed==SIZE_MAX);
        if(mode==0){
            assert(fnv_vk_prepare_programs(gpu,fnv_startup_programs,2,catalog,count-1,&failed)==FNV_VK_INVALID && failed==1);
            assert(!fnv_vk_backend_stats(gpu,&after) && !after.shader_catalog_entries && !after.linked_programs);
            const struct fnv_vk_prepared_program *missing[2]={fnv_startup_programs[0],NULL};
            assert(fnv_vk_prepare_programs(gpu,missing,2,catalog,count,&failed)==FNV_VK_INVALID && failed==1);
            assert(!fnv_vk_backend_stats(gpu,&after) && !after.shader_catalog_entries && !after.linked_programs);
        }else if(mode==1){
            struct fnv_vk_prepared_program invalid=*fnv_startup_programs[1];invalid.stages[1].plan.bytes=65537;
            const struct fnv_vk_prepared_program *batch[2]={fnv_startup_programs[0],&invalid};
            assert(fnv_vk_prepare_programs(gpu,batch,2,catalog,count,&failed)==FNV_VK_INVALID && failed==1);
            assert(!fnv_vk_backend_stats(gpu,&after) && after.shader_catalog_entries>=2 && after.linked_programs==1);
        }else {
            for(unsigned i=0;i<64;i++)assert(!fnv_vk_prepare_programs(gpu,fnv_startup_programs,2,catalog,count,&failed) && failed==SIZE_MAX);
            assert(!fnv_vk_backend_stats(gpu,&after) && after.shader_catalog_entries==3 && after.linked_programs==2);
        }
        assert(after.allocated_bytes==before.allocated_bytes && !after.live_shaders && !after.draws && !after.submissions);
        struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);b.device_release(gpu);assert(!fnv_vk_backend_destroy(gpu));
    }
    FreeLibrary(loader);puts("PASS bundle preflight, failed index, partial teardown and 64 repeated batches");return 0;
}
