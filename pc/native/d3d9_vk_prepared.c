#include "d3d9_vk_prepared.h"
#include <string.h>
static uint32_t validate_program(const struct fnv_vk_prepared_program *p,const struct fnv_d3d9_shader_code *catalog,size_t count){
    if(!p || !catalog || !count || count>8192 || !p->inputs || !p->input_count || p->input_count>32 || p->sampler_mask&~65535u)return FNV_VK_INVALID;
    for(unsigned stage=0;stage<2;stage++){
        const struct fnv_vk_prepared_stage *s=p->stages+stage;int found=0;
        if(!s->code || s->code_bytes<8 || s->code_bytes>1048576 || s->code_bytes%4 || !s->spirv || s->spirv_bytes<20 || s->spirv_bytes>1048576 || s->spirv_bytes%4)return FNV_VK_INVALID;
        for(size_t i=0;i<count;i++)if(catalog[i].bytes && catalog[i].size==s->code_bytes && !memcmp(catalog[i].bytes,s->code,s->code_bytes)){found=1;break;}
        if(!found)return FNV_VK_INVALID;
    }
    return 0;
}
uint32_t fnv_vk_prepare_programs(struct fnv_vk_backend *gpu,const struct fnv_vk_prepared_program *const *programs,size_t programs_count,const struct fnv_d3d9_shader_code *catalog,size_t count,size_t *failed_index){
    if(failed_index)*failed_index=SIZE_MAX;
    if(!gpu || !programs || !programs_count || programs_count>4096)return FNV_VK_INVALID;
    for(size_t i=0;i<programs_count;i++){
        uint32_t hr=validate_program(programs[i],catalog,count);
        if(hr){if(failed_index)*failed_index=i;return hr;}
    }
    for(size_t i=0;i<programs_count;i++){
        uint32_t hr=fnv_vk_backend_prepare_variant(gpu,programs[i]);
        if(hr){if(failed_index)*failed_index=i;return hr;}
    }
    return 0;
}
uint32_t fnv_vk_prepare_program(struct fnv_vk_backend *gpu,const struct fnv_vk_prepared_program *p,const struct fnv_d3d9_shader_code *catalog,size_t count){return fnv_vk_prepare_programs(gpu,&p,1,catalog,count,NULL);}
