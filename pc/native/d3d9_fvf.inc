static void api_get_fvf(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);unsigned char *out=range(c,fnv_cpu_argument_u32(1),4,1);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(out){write32(out,c->declaration<0?0:c->resources[c->declaration].fvf);hr=0;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,8);
}
static void api_set_fvf(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),fvf=fnv_cpu_argument_u32(1);struct fnv_fvf_layout plan;
    uint32_t hr=fnv_d3d9_fvf_layout(fvf,&plan);fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(hr)goto done;
    if(!c->config.backend.create_declaration || !c->config.backend.bind_declaration){hr=0x8876086au;goto done;}
    /* Match the initial/null state and avoid replacing an already matching FVF. */
    if(!fvf && c->declaration<0)goto done;
    for(uint32_t i=0;i<c->used;++i){struct resource *r=c->resources+i;
        if(r->handle && r->kind==FNV_D3D9_DECL && r->fvf==fvf && r->element_count==plan.count && !memcmp(r->elements,plan.elements,plan.count*8)){
            hr=c->config.backend.bind_declaration(c->config.backend.context,r->handle);if(!hr)change_binding(c,&c->declaration,(int)i);goto done;
        }
    }
    uint32_t index=0;while(index<c->used && c->resources[index].handle)index++;
    if(index==c->config.resource_capacity){hr=FNV_D3D9_OUTOFMEMORY;goto done;}
    unsigned char *elements=malloc(plan.count*8);if(!elements){hr=FNV_D3D9_OUTOFMEMORY;goto done;}memcpy(elements,plan.elements,plan.count*8);
    uintptr_t handle=0;hr=c->config.backend.create_declaration(c->config.backend.context,elements,plan.count,&handle);
    if(!hr && !handle){free(elements);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 FVF provider returned no declaration",fvf);}
    if(!hr)for(uint32_t i=0;i<c->used;++i)if(c->resources[i].handle==handle){c->config.backend.release(c->config.backend.context,handle);free(elements);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 FVF provider reused declaration identity",fvf);}
    if(!hr)hr=c->config.backend.bind_declaration(c->config.backend.context,handle);
    if(!hr){c->resources[index]=(struct resource){.handle=handle,.kind=FNV_D3D9_DECL,.elements=elements,.element_count=plan.count,.fvf=fvf};
        if(index==c->used)c->used++;
        write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+DECL_VTABLE);change_binding(c,&c->declaration,(int)index);
    }else free(elements);
    /* The binding, rather than a hidden public reference, owns the declaration. */
    if(handle)c->config.backend.release(c->config.backend.context,handle);
done:fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,8);
}
