/* Numeric and texture-binding transaction under the provider lock. */
struct numeric_state {
    struct fnv_d3d9_blend blend;struct fnv_d3d9_depth depth;struct fnv_d3d9_sampler samplers[16];
    uint32_t cull,viewport[6],scissor[4],scissor_enabled;
};
static uint32_t state_batch(void *ctx,struct fnv_d3d9_state_value *entries,uint32_t count,uint32_t operation){
    struct fnv_vk_backend *d=ctx;if(count>272 || (count && !entries) || operation>2)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=0;
    if(d->lost || !d->device_refs){fnv_host_mutex_unlock(&d->guard);return FNV_VK_LOST;}
    struct numeric_state old={.blend=d->blend_state,.depth=d->depth_state,.cull=d->cull_mode,.scissor_enabled=d->scissor_enabled};
    memcpy(old.samplers,d->sampler_state,sizeof(old.samplers));memcpy(old.viewport,d->viewport,sizeof(old.viewport));memcpy(old.scissor,d->scissor,sizeof(old.scissor));
    uintptr_t textures[16];memcpy(textures,d->texture_bindings,sizeof(textures));
    struct fnv_d3d9_state_value candidate[272];if(count)memcpy(candidate,entries,count*sizeof(*entries));
    for(uint32_t i=0;i<count;++i){struct fnv_d3d9_state_value *e=candidate+i;
        if(e->kind==FNV_D3D9_TEXTURE_BINDING){
            if(e->sampler>=16 || e->name){hr=FNV_VK_INVALID;break;}
            if(operation==1){uint64_t id=textures[e->sampler];e->values[0]=(uint32_t)id;e->values[1]=(uint32_t)(id>>32);}
            else {uint64_t id=(uint64_t)e->values[0]|(uint64_t)e->values[1]<<32;
                if(id>UINTPTR_MAX || (id && !find_texture(d,(uintptr_t)id))){hr=FNV_VK_INVALID;break;}
                textures[e->sampler]=(uintptr_t)id;}
        }else hr=state_locked(d,e->kind,e->sampler,e->name,e->values,operation==1);
        if(hr)break;
    }
    if(hr || operation!=0){d->blend_state=old.blend;d->depth_state=old.depth;d->cull_mode=old.cull;d->scissor_enabled=old.scissor_enabled;
        memcpy(d->sampler_state,old.samplers,sizeof(old.samplers));memcpy(d->viewport,old.viewport,sizeof(old.viewport));memcpy(d->scissor,old.scissor,sizeof(old.scissor));}
    if(!hr && operation==1 && count)memcpy(entries,candidate,count*sizeof(*entries));
    if(!hr && operation==0){
        /* Acquire all new pins before dropping any old binding. */
        for(unsigned i=0;i<16;++i)if(textures[i]!=d->texture_bindings[i])pin(d,textures[i],1);
        for(unsigned i=0;i<16;++i)if(textures[i]!=d->texture_bindings[i]){uintptr_t old_id=d->texture_bindings[i];d->texture_bindings[i]=textures[i];pin(d,old_id,0);}
    }
    fnv_host_mutex_unlock(&d->guard);return hr;
}
