/* Fixed prepared configuration only; no hidden resize/WSI recovery. */
static void api_cooperative(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    uint32_t hr=0x8876086au;
    if(c->creator && c->creator!=(uintptr_t)&creator_marker)hr=FNV_D3D9_INVALIDCALL;
    else if(c->config.backend.cooperative){hr=c->config.backend.cooperative(c->config.backend.context);if(!hr && c->reset_failed)hr=0x88760869u;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,4);
}
static void api_reset(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);unsigned char *out=range(c,fnv_cpu_argument_u32(1),56,1);uint32_t values[14],hr=FNV_D3D9_INVALIDCALL;
    struct fnv_d3d9_constants *fresh=NULL;
    if(out)for(unsigned i=0;i<14;++i)values[i]=read32(out+i*4);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(!c->config.factory || !c->factory_created || !c->config.reset_timeout_ns || !c->config.backend.reset){hr=0x8876086au;goto done;}
    if(c->creator!=(uintptr_t)&creator_marker)goto done;
    if(c->recording){c->record_failed=1;goto done;}
    if(!out || c->in_scene)goto failure;
    values[8]=values[8]!=0;values[9]=values[9]!=0;
    if(values[8]){if(!values[0])values[0]=c->factory.width;if(!values[1])values[1]=c->factory.height;if(!values[2])values[2]=c->factory.color_format;}
    if(!values[3])values[3]=1;
    if(values[0]!=c->factory.width || values[1]!=c->factory.height || values[2]!=c->factory.color_format || values[3]!=1 || values[4] || values[5] || values[6]!=1 ||
       (values[7] && values[7]!=c->config.presentation_window) || values[8]!=c->present_params[8] ||
       (values[9]!=0)!=(c->factory.depth_format!=0) || (values[9] && values[10]!=c->factory.depth_format) || (values[11]&~2u) ||
       values[12]!=c->present_params[12] || values[13]!=c->present_params[13])goto failure;
    for(uint32_t i=0;i<c->used;++i){struct resource *r=c->resources+i;
        if(!r->handle)continue;
        if(r->locked || ((r->references || r->state_pins) && (r->kind==FNV_D3D9_SURFACE || ((r->kind==FNV_D3D9_VB || r->kind==FNV_D3D9_IB || r->kind==FNV_D3D9_TEXTURE2D || r->kind==FNV_D3D9_TEXTURECUBE) && !r->pool))))goto failure;
        for(uint32_t level=0;level<r->levels*(r->faces?r->faces:1);++level)if(r->mips[level].locked)goto failure;
    }
    hr=fnv_d3d9_constants_create(c->config.vertex_floats,c->config.pixel_floats,&fresh);if(hr)goto failure;
    hr=c->config.backend.reset(c->config.backend.context,c->config.reset_timeout_ns);if(hr)goto failure;
    fnv_transform_init(&c->transforms);memset(c->material,0,sizeof(c->material));c->lights.count=0;
    clear_bindings(c);for(unsigned i=0;i<16;++i)c->streams[i]=(struct stream){-1,0,0,1};
    struct fnv_d3d9_constants *old=c->constants;c->constants=fresh;fresh=NULL;fnv_d3d9_constants_destroy(old);
    c->reset_failed=0;memcpy(c->present_params,values,sizeof(values));
    for(unsigned i=0;i<14;++i)write32(out+i*4,values[i]);
    goto done;
failure:c->reset_failed=1;
done:if(fresh)fnv_d3d9_constants_destroy(fresh);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,8);
}
