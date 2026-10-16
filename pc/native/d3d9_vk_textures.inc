/* Private implementation included by d3d9_vk_backend.c; shares its lock/queue. */
static uint32_t mip_size(uint32_t size,uint32_t level){size>>=level;return size?size:1;}
static VkFormat bc_format(uint32_t format){
    switch(format){case 0x31545844:return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;case 0x33545844:return VK_FORMAT_BC2_UNORM_BLOCK;case 0x35545844:return VK_FORMAT_BC3_UNORM_BLOCK;default:return VK_FORMAT_UNDEFINED;}
}
static uint32_t bc_unit(uint32_t format){return format==0x31545844?8:16;}
static size_t texture_level_bytes(const struct texture_resource *t,uint32_t level){
    uint32_t w=mip_size(t->width,level),h=mip_size(t->height,level);
    return bc_format(t->format)?(size_t)((w+3)/4)*((h+3)/4)*bc_unit(t->format):(size_t)w*h*4;
}
static void texture_barrier(struct fnv_vk_backend *d,VkCommandBuffer cmd,struct texture_version *v,VkImageLayout old,VkAccessFlags source,VkAccessFlags dest,uint32_t levels){
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=source,.dstAccessMask=dest,
        .oldLayout=old,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=v->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,levels,0,1}};
    d->v.CmdPipelineBarrier(cmd,old==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
}
static uint32_t make_texture_version(struct fnv_vk_backend *d,const struct texture_resource *t,struct texture_version **out){
    VkFormat format=bc_format(t->format);if(!format)format=VK_FORMAT_R8G8B8A8_UNORM;
    VkImageFormatProperties caps;VkImageUsageFlags usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkResult query=d->v.GetPhysicalDeviceImageFormatProperties(d->physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&caps);
    if(query==VK_ERROR_FORMAT_NOT_SUPPORTED)return FNV_VK_INVALID;
    uint32_t hr=result(d,query);if(hr)return hr;
    if(t->width>caps.maxExtent.width || t->height>caps.maxExtent.height || t->levels>caps.maxMipLevels || !(caps.sampleCounts&VK_SAMPLE_COUNT_1_BIT))return FNV_VK_INVALID;
    struct texture_version *v=calloc(1,sizeof(*v));if(!v)return FNV_VK_OOM;
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
        .extent={t->width,t->height,1},.mipLevels=t->levels,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    VkImage image=VK_NULL_HANDLE;hr=result(d,d->v.CreateImage(d->device,&ci,NULL,&image));
    if(hr){free(v);return hr;}v->image=image;
    VkMemoryRequirements req;d->v.GetImageMemoryRequirements(d->device,v->image,&req);
    hr=allocate(d,req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&v->memory,NULL);
    if(!hr){v->allocation=req.size;hr=result(d,d->v.BindImageMemory(d->device,v->image,v->memory,0));}
    if(!hr){VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=v->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
        .format=format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,t->levels,0,1}};
        VkImageView view=VK_NULL_HANDLE;hr=result(d,d->v.CreateImageView(d->device,&vi,NULL,&view));if(!hr)v->view=view;}
    if(hr){d->v.DestroyImage(d->device,v->image,NULL);if(v->memory){d->v.FreeMemory(d->device,v->memory,NULL);d->allocated-=v->allocation;}free(v);return hr;}
    v->refs=1;d->texture_versions++;*out=v;return 0;
}
static uint32_t create_texture(void *ctx,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;
    if(!out || !w || !h || w>32768 || h>32768 || pool>1 || (usage && usage!=512) || (usage && pool) ||
       (format!=21 && format!=22 && format!=23 && format!=28 && format!=50 && !bc_format(format)))return FNV_VK_INVALID;
    uint32_t max=1;for(uint32_t n=w>h?w:h;n>1;n>>=1)max++;
    if(!levels)levels=max;
    if(levels>max || levels>16)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(bc_format(format) && !d->bc)goto done;
    if(d->next_id==UINTPTR_MAX)goto done;
    if(resource_count(d)>=d->config.resource_capacity){hr=FNV_VK_OOM;goto done;}
    struct texture_resource *slot=NULL;for(uint32_t i=0;i<d->config.resource_capacity;++i)if(!d->textures[i].id){slot=d->textures+i;break;}
    struct texture_resource t={.width=w,.height=h,.levels=levels,.usage=usage,.format=format,.pool=pool};
    for(uint32_t i=0;i<levels;++i){t.offsets[i]=t.bytes;t.bytes+=texture_level_bytes(&t,i);}
    if(t.bytes>d->config.memory_budget){hr=FNV_VK_OOM;goto done;}
    t.shadow=calloc(1,t.bytes);if(!t.shadow){hr=FNV_VK_OOM;goto done;}
    hr=make_texture_version(d,&t,&t.version);if(hr){free(t.shadow);goto done;}
    t.id=++d->next_id;t.refs=1;*slot=t;d->device_refs++;*out=t.id;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t upload_texture(void *ctx,uintptr_t id,uint32_t level,uint32_t left,uint32_t top,uint32_t width,uint32_t height,uint32_t flags,uint32_t pitch,const void *data){
    struct fnv_vk_backend *d=ctx;
    if(!data || !width || !height || (flags&~(0x800u|0x2000u)))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;struct texture_resource *t=find_texture(d,id);
    unsigned char *candidate=NULL;struct texture_version *v=NULL;struct frame *f=NULL;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(!t || level>=t->levels || ((flags&0x2000u) && (level || !(t->usage&512) || left || top || width!=t->width || height!=t->height)))goto done;
    candidate=malloc(t->bytes);if(!candidate){hr=FNV_VK_OOM;goto done;}
    if(flags&0x2000u)memset(candidate,0,t->bytes);else memcpy(candidate,t->shadow,t->bytes);
    uint32_t unit=t->format==23?2:t->format==28 || t->format==50?1:4;
    uint64_t source_bytes=bc_format(t->format)?(uint64_t)((height+3)/4-1)*pitch+(uint64_t)((width+3)/4)*bc_unit(t->format):(uint64_t)(height-1)*pitch+(uint64_t)width*unit;
    if(source_bytes>SIZE_MAX || source_bytes>UINTPTR_MAX-(uintptr_t)data)goto done;
    struct fnv_texture_patch patch={mip_size(t->width,level),mip_size(t->height,level),left,top,width,height,t->format,pitch,data,(size_t)source_bytes};
    if(bc_format(t->format)){
        uint32_t block=bc_unit(t->format);
        if(left>=patch.width || top>=patch.height || width>patch.width-left || height>patch.height-top || left%4 || top%4 ||
           ((left+width)!=patch.width && width%4) || ((top+height)!=patch.height && height%4))goto done;
        size_t row=(size_t)((width+3)/4)*block;uint32_t rows=(height+3)/4;
        if(pitch<row || (uint64_t)(rows-1)*pitch+row>SIZE_MAX)goto done;
        size_t destination_pitch=(size_t)((patch.width+3)/4)*block;
        for(uint32_t y=0;y<rows;++y)memcpy(candidate+t->offsets[level]+(top/4+y)*destination_pitch+(left/4)*block,(const unsigned char*)data+(size_t)y*pitch,row);
    }else {hr=fnv_d3d9_texture_rgba8(&patch,candidate+t->offsets[level],t->bytes-t->offsets[level],patch.width*4);if(hr)goto done;}
    hr=make_texture_version(d,t,&v);if(hr)goto done;
    /* Explicit backpressure: no CPU wait or fabricated completion on upload. */
    hr=begin_frame(d,&f);if(hr)goto done;
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=t->bytes,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
    VkBuffer buffer=VK_NULL_HANDLE;hr=result(d,d->v.CreateBuffer(d->device,&ci,NULL,&buffer));if(!hr)f->read_buffer=buffer;
    if(!hr){VkMemoryRequirements req;d->v.GetBufferMemoryRequirements(d->device,f->read_buffer,&req);
        hr=allocate(d,req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&f->read_memory,&f->read_coherent);
        if(!hr){f->read_allocation=req.size;hr=result(d,d->v.BindBufferMemory(d->device,f->read_buffer,f->read_memory,0));}}
    void *mapped=NULL;if(!hr)hr=result(d,d->v.MapMemory(d->device,f->read_memory,0,VK_WHOLE_SIZE,0,&mapped));
    if(!hr){memcpy(mapped,candidate,t->bytes);
        if(!f->read_coherent){VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=f->read_memory,.offset=0,.size=VK_WHOLE_SIZE};
            hr=result(d,d->v.FlushMappedMemoryRanges(d->device,1,&range));}
        d->v.UnmapMemory(d->device,f->read_memory);}
    if(hr){retire_frame(d,f);f=NULL;goto done;}
    f->read_texture=v;v->refs++;f->read_surface=id;pin(d,id,1);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT};
    d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&host,0,NULL,0,NULL);
    texture_barrier(d,f->cmd,v,VK_IMAGE_LAYOUT_UNDEFINED,0,VK_ACCESS_TRANSFER_WRITE_BIT,t->levels);
    VkBufferImageCopy copies[16];memset(copies,0,sizeof(copies));
    for(uint32_t i=0;i<t->levels;++i){copies[i].bufferOffset=t->offsets[i];copies[i].imageSubresource=(VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT,i,0,1};
        copies[i].imageExtent=(VkExtent3D){mip_size(t->width,i),mip_size(t->height,i),1};}
    d->v.CmdCopyBufferToImage(f->cmd,f->read_buffer,v->image,VK_IMAGE_LAYOUT_GENERAL,t->levels,copies);
    texture_barrier(d,f->cmd,v,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT,t->levels);
    hr=submit_frame(d,f);if(hr){d->lost=1;goto done;}
    v->initialized=1;struct texture_version *old=t->version;t->version=v;v=NULL;release_texture_version(d,old);
    free(t->shadow);t->shadow=candidate;candidate=NULL;
done:release_texture_version(d,v);free(candidate);fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t bind_texture(void *ctx,uint32_t sampler,uintptr_t id){
    struct fnv_vk_backend *d=ctx;if(sampler>=16)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;
    else if(!id || find_texture(d,id)){uintptr_t old=d->texture_bindings[sampler];pin(d,id,1);d->texture_bindings[sampler]=id;pin(d,old,0);hr=0;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
