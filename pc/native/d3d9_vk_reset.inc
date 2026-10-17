static void discard_surface(struct fnv_vk_backend *d,struct surface *s){
    if(s->view)d->v.DestroyImageView(d->device,s->view,NULL);
    if(s->image)d->v.DestroyImage(d->device,s->image,NULL);
    if(s->memory){d->v.FreeMemory(d->device,s->memory,NULL);d->allocated-=s->allocation;}
    memset(s,0,sizeof(*s));
}
uint32_t fnv_vk_backend_reset(struct fnv_vk_backend *d,uint64_t timeout){
    if(!d || !timeout || timeout==UINT64_MAX)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    struct surface color={0},depth={0};struct fnv_uniform_state *uniforms=NULL;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(d->in_scene || d->next_id>UINTPTR_MAX-2)goto done;
    for(uint32_t i=0;i<d->config.resource_capacity;++i){
        if(d->surfaces[i].refs || (d->buffer_resources[i].refs && !d->buffer_resources[i].pool) ||
           ((d->textures[i].refs || d->textures[i].block_pins) && !d->textures[i].pool))goto done;
    }
    struct surface *old_color=find(d,d->back),*old_depth=find(d,d->auto_depth);
    if(!old_color || (d->config.auto_depth && !old_depth))goto done;
    uint32_t max=d->properties.limits.maxUniformBufferRange;if(max>65536)max=65536;max&=~15u;
    uint32_t slots=d->config.frames*d->config.commands;if(slots>64)slots=64;
    struct fnv_uniform_config uc={256,224,slots,max};hr=fnv_uniform_state_create(&uc,&uniforms);if(hr)goto done;
    hr=build_surface(d,0,d->config.width,d->config.height,0,0,&color);if(hr)goto done;
    if(d->config.auto_depth){hr=build_surface(d,1,d->config.width,d->config.height,0,0,&depth);if(hr)goto done;}
    /* No mutable binding/default state is committed before render completion.
     * Failed allocation leaves old targets/state usable by the host. */
    hr=wait_locked(d,timeout);if(hr){if(hr==FNV_VK_BUSY){d->lost=1;hr=FNV_VK_LOST;}goto done;}
    for(uint32_t i=0;i<d->count;++i){pin(d,d->commands[i].color,0);pin(d,d->commands[i].depth,0);}d->count=0;
    close_device(d); /* Drop internal refs, retaining all public managed/shader refs. */
    struct fnv_uniform_state *previous=d->uniforms;d->uniforms=uniforms;uniforms=NULL;(void)fnv_uniform_state_destroy(previous);
    color.id=++d->next_id;*old_color=color;memset(&color,0,sizeof(color));d->back=old_color->id;pin(d,d->back,1);d->color=d->back;pin(d,d->color,1);
    if(d->config.auto_depth){depth.id=++d->next_id;*old_depth=depth;memset(&depth,0,sizeof(depth));d->auto_depth=old_depth->id;pin(d,d->auto_depth,1);d->depth=d->auto_depth;pin(d,d->depth,1);}
    d->blend_state=(struct fnv_d3d9_blend){0,2,1,1,0,2,1,1,15,0xffffffff};d->depth_state=(struct fnv_d3d9_depth){1,1,4};d->cull_mode=3;
    for(unsigned i=0;i<16;++i)d->sampler_state[i]=(struct fnv_d3d9_sampler){1,1,1,0,1,1,0,0,0,1};
    full_viewport(d,old_color);d->scissor_enabled=0;d->scissor[0]=d->scissor[1]=0;d->scissor[2]=d->config.width;d->scissor[3]=d->config.height;
    d->resets++;hr=0;
done:
    discard_surface(d,&color);discard_surface(d,&depth);if(uniforms)(void)fnv_uniform_state_destroy(uniforms);
    fnv_host_mutex_unlock(&d->guard);return hr;
}
