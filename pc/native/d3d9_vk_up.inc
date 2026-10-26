/* Transient versions do not consume public resource slots or guest identities. */
static uint32_t draw_up(void *ctx,int indexed,uint32_t primitive,uint32_t minimum,uint32_t vertices,uint32_t primitives,const void *indices,uint32_t format,const void *data,uint32_t stride,uint32_t vertex_bytes,uint32_t index_bytes){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    struct buffer_resource vb={.kind=FNV_D3D9_VB,.bytes=vertex_bytes},ib={.kind=FNV_D3D9_IB,.bytes=index_bytes,.format=format,.shadow=(unsigned char*)indices};
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    uint64_t count=primitive==4?(uint64_t)primitives*3:(uint64_t)primitives+2;
    if(indexed<0 || indexed>1 || !data || !stride || primitive<4 || primitive>6 || !primitives || count>UINT32_MAX ||
       (indexed && (!indices || !vertices || (uint64_t)minimum+vertices>UINT32_MAX || (format!=101 && format!=102))) ||
       (indexed?(uint64_t)minimum+vertices:count)*stride!=vertex_bytes ||
       (indexed?count*(format==101?2u:4u):0)!=index_bytes)goto done;
    struct declaration_resource *decl=find_declaration(d,d->declaration);if(!decl)goto done;
    for(uint32_t i=0;i+1<decl->count;++i)if(decl->elements[i*8] || decl->elements[i*8+1])goto done;
    if((uint64_t)vertex_bytes+index_bytes>d->config.memory_budget){hr=FNV_VK_OOM;goto done;}
    hr=make_version(d,FNV_D3D9_VB,vertex_bytes,data,&vb.version);if(hr)goto done;
    if(indexed){hr=make_version(d,FNV_D3D9_IB,index_bytes,indices,&ib.version);if(hr)goto done;}
    if(!d->bound_shaders[0] && !d->bound_shaders[1]){
        /* Narrow XYZRHW/TEX1 path: no diffuse input, stage-zero CURRENT is
         * therefore white. Other fixed-function combinations remain pending. */
        static const unsigned char layout[24]={0,0,0,0,3,0,9,0,0,0,16,0,1,0,5,0,255,0,0,0,17,0,0,0};
        const uint32_t *t=d->texture_stage[0];float lo,hi;memcpy(&lo,d->viewport+4,4);memcpy(&hi,d->viewport+5,4);
        if(decl->count!=3 || memcmp(decl->elements,layout,sizeof layout) || stride!=24 || d->lighting || d->depth_state.enable ||
           d->texture_coord_index[0] || d->texture_stage[1][0]!=1 || t[0]!=4 || t[1]!=2 || t[2]!=1 || t[3]!=2 || t[4]!=2 ||
           lo!=0 || hi!=1 || !d->viewport[2] || !d->viewport[3] || !d->fixed_screen[0] || !d->fixed_screen[1]){hr=0x8876086au;goto done;}
        float viewport[4]={2.f/(float)d->viewport[2],-2.f/(float)d->viewport[3],
            -1.f-2.f*(float)d->viewport[0]/(float)d->viewport[2],1.f+2.f*(float)d->viewport[1]/(float)d->viewport[3]};
        hr=draw_locked(d,indexed,primitive,0,minimum,vertices,0,primitives,&vb,indexed?&ib:NULL,stride,viewport,16);
    }else hr=draw_locked(d,indexed,primitive,0,minimum,vertices,0,primitives,&vb,indexed?&ib:NULL,stride,NULL,0);
done:
    release_version(d,vb.version);release_version(d,ib.version);
    uintptr_t old=d->streams[0].id;d->streams[0]=(struct stream_binding){0};pin(d,old,0);
    if(indexed){old=d->indices;d->indices=0;pin(d,old,0);}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
