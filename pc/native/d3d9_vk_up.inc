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
    hr=draw_locked(d,indexed,primitive,0,minimum,vertices,0,primitives,&vb,indexed?&ib:NULL,stride);
done:
    release_version(d,vb.version);release_version(d,ib.version);
    uintptr_t old=d->streams[0].id;d->streams[0]=(struct stream_binding){0};pin(d,old,0);
    if(indexed){old=d->indices;d->indices=0;pin(d,old,0);}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
