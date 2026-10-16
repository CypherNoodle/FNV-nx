/* Numeric state only. The whole transaction is protected by the provider lock. */
struct numeric_state {
    struct fnv_d3d9_blend blend;struct fnv_d3d9_depth depth;struct fnv_d3d9_sampler samplers[16];
    uint32_t cull,viewport[6],scissor[4],scissor_enabled;
};
static uint32_t state_batch(void *ctx,struct fnv_d3d9_state_value *entries,uint32_t count,uint32_t operation){
    struct fnv_vk_backend *d=ctx;if(count>256 || (count && !entries) || operation>2)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=0;
    if(d->lost || !d->device_refs){fnv_host_mutex_unlock(&d->guard);return FNV_VK_LOST;}
    struct numeric_state old={.blend=d->blend_state,.depth=d->depth_state,.cull=d->cull_mode,.scissor_enabled=d->scissor_enabled};
    memcpy(old.samplers,d->sampler_state,sizeof(old.samplers));memcpy(old.viewport,d->viewport,sizeof(old.viewport));memcpy(old.scissor,d->scissor,sizeof(old.scissor));
    struct fnv_d3d9_state_value candidate[256];if(count)memcpy(candidate,entries,count*sizeof(*entries));
    for(uint32_t i=0;i<count;++i){struct fnv_d3d9_state_value *e=candidate+i;
        hr=state_locked(d,e->kind,e->sampler,e->name,e->values,operation==1);if(hr)break;
    }
    if(hr || operation!=0){d->blend_state=old.blend;d->depth_state=old.depth;d->cull_mode=old.cull;d->scissor_enabled=old.scissor_enabled;
        memcpy(d->sampler_state,old.samplers,sizeof(old.samplers));memcpy(d->viewport,old.viewport,sizeof(old.viewport));memcpy(d->scissor,old.scissor,sizeof(old.scissor));}
    if(!hr && operation==1 && count)memcpy(entries,candidate,count*sizeof(*entries));
    fnv_host_mutex_unlock(&d->guard);return hr;
}
