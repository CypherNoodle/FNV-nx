/* Test-only dispatch proxy. Real Vulkan handles/work; deterministic injected
 * API failures verify rollback and lifetime without requiring driver faults. */
static PFN_vkGetInstanceProcAddr original_gpa;
static PFN_vkGetDeviceProcAddr original_gdpa;
static unsigned hide_bc,force_bc_opaque;
static PFN_vkGetPhysicalDeviceFeatures original_features;
static VKAPI_ATTR void VKAPI_CALL proxy_features(VkPhysicalDevice d,VkPhysicalDeviceFeatures *out){original_features(d,out);if(hide_bc)out->textureCompressionBC=VK_FALSE;}

static unsigned fail_memory,fail_map,fail_flush,timeout_fence,fail_submit,fail_idle,invalidations,flushes;
static unsigned fail_pipeline,live_Pipeline,fail_descriptors,force_clamp,force_wrong_cull,force_wrong_blend;
static PFN_vkCreateGraphicsPipelines original_pipelines;
static PFN_vkDestroyPipeline original_destroy_pipeline;
static PFN_vkAllocateDescriptorSets original_descriptor_sets;
static VKAPI_ATTR VkResult VKAPI_CALL proxy_pipelines(VkDevice d,VkPipelineCache cache,uint32_t n,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *a,VkPipeline *out){
    assert(n==1 && cache!=VK_NULL_HANDLE);VkGraphicsPipelineCreateInfo next=*info;VkPipelineRasterizationStateCreateInfo raster=*info->pRasterizationState;
    VkPipelineColorBlendStateCreateInfo blend=*info->pColorBlendState;VkPipelineColorBlendAttachmentState attachment=*blend.pAttachments;
    if(force_wrong_cull && raster.cullMode!=VK_CULL_MODE_NONE){raster.frontFace=raster.frontFace==VK_FRONT_FACE_CLOCKWISE?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE;next.pRasterizationState=&raster;}
    if(force_wrong_blend && attachment.blendEnable){attachment.blendEnable=VK_FALSE;blend.pAttachments=&attachment;next.pColorBlendState=&blend;}
    VkResult result=original_pipelines(d,cache,n,&next,a,out);if(out[0])live_Pipeline++;
    if(!result && fail_pipeline){fail_pipeline--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return result;
}
static VKAPI_ATTR void VKAPI_CALL proxy_destroy_pipeline(VkDevice d,VkPipeline pipeline,const VkAllocationCallbacks *a){
    if(pipeline){assert(live_Pipeline);live_Pipeline--;}original_destroy_pipeline(d,pipeline,a);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_descriptor_sets(VkDevice d,const VkDescriptorSetAllocateInfo *info,VkDescriptorSet *out){
    if(fail_descriptors){fail_descriptors--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return original_descriptor_sets(d,info,out);
}
static int force_noncoherent,hide_bc_linear;
static PFN_vkGetPhysicalDeviceFormatProperties original_format_properties;
static VkFormat last_vertex_format;
static VkFormatFeatureFlags last_vertex_features;
static VKAPI_ATTR void VKAPI_CALL proxy_format_properties(VkPhysicalDevice p,VkFormat format,VkFormatProperties *out){
    original_format_properties(p,format,out);last_vertex_format=format;last_vertex_features=out->bufferFeatures;
    if(hide_bc_linear && (format==VK_FORMAT_BC1_RGBA_UNORM_BLOCK || format==VK_FORMAT_BC2_UNORM_BLOCK || format==VK_FORMAT_BC3_UNORM_BLOCK))out->optimalTilingFeatures&=~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
}
static unsigned fail_end,fail_reset;
static PFN_vkEndCommandBuffer original_end;
static PFN_vkResetFences original_reset;
static VKAPI_ATTR VkResult VKAPI_CALL proxy_end(VkCommandBuffer cmd){
    if(fail_end){fail_end--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return original_end(cmd);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_reset(VkDevice d,uint32_t n,const VkFence *fences){
    if(fail_reset){fail_reset--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return original_reset(d,n,fences);
}
#define OBJECTS(X) X(Image) X(ImageView) X(Buffer) X(RenderPass) X(Framebuffer) X(CommandPool) X(Fence) X(ShaderModule) X(Sampler) X(DescriptorSetLayout) X(PipelineLayout) X(DescriptorPool) X(PipelineCache)
#define OBJECT(n) \
static PFN_vkCreate##n original_create_##n;static PFN_vkDestroy##n original_destroy_##n; \
static unsigned live_##n,fail_##n; \
static VKAPI_ATTR VkResult VKAPI_CALL proxy_create_##n(VkDevice d,const Vk##n##CreateInfo *info,const VkAllocationCallbacks *a,Vk##n *out){ \
    if(fail_##n){fail_##n--;*out=(Vk##n)(uintptr_t)0xdeadbad;return VK_ERROR_OUT_OF_HOST_MEMORY;} \
    VkResult r=original_create_##n(d,info,a,out);if(!r)live_##n++;return r;} \
static VKAPI_ATTR void VKAPI_CALL proxy_destroy_##n(VkDevice d,Vk##n h,const VkAllocationCallbacks *a){ \
    if(h){assert(live_##n);live_##n--;}original_destroy_##n(d,h,a);}
OBJECTS(OBJECT)
#undef OBJECT
static VKAPI_ATTR VkResult VKAPI_CALL proxy_imageview_override(VkDevice d,const VkImageViewCreateInfo *info,const VkAllocationCallbacks *a,VkImageView *out){
    VkImageViewCreateInfo next=*info;if(force_bc_opaque && next.format==VK_FORMAT_BC1_RGBA_UNORM_BLOCK)next.components.a=VK_COMPONENT_SWIZZLE_ONE;
    return proxy_create_ImageView(d,&next,a,out);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_sampler_override(VkDevice d,const VkSamplerCreateInfo *info,const VkAllocationCallbacks *a,VkSampler *out){
    VkSamplerCreateInfo next=*info;if(force_clamp)next.addressModeU=next.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    return proxy_create_Sampler(d,&next,a,out);
}
static PFN_vkAllocateMemory original_allocate;
static PFN_vkFreeMemory original_free;
static PFN_vkMapMemory original_map;
static PFN_vkWaitForFences original_wait;
static PFN_vkQueueSubmit original_submit;
static PFN_vkDeviceWaitIdle original_idle;
static PFN_vkInvalidateMappedMemoryRanges original_invalidate;
static PFN_vkFlushMappedMemoryRanges original_flush;
static PFN_vkGetPhysicalDeviceMemoryProperties original_memory_properties;
static unsigned live_memory;
static VKAPI_ATTR VkResult VKAPI_CALL proxy_allocate(VkDevice d,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *a,VkDeviceMemory *out){
    if(fail_memory){fail_memory--;*out=(VkDeviceMemory)(uintptr_t)0xdeadbad;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}
    VkResult r=original_allocate(d,info,a,out);if(!r)live_memory++;return r;
}
static VKAPI_ATTR void VKAPI_CALL proxy_free(VkDevice d,VkDeviceMemory h,const VkAllocationCallbacks *a){
    if(h){assert(live_memory);live_memory--;}original_free(d,h,a);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_map(VkDevice d,VkDeviceMemory h,VkDeviceSize offset,VkDeviceSize size,VkMemoryMapFlags flags,void **out){
    if(fail_map){fail_map--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return original_map(d,h,offset,size,flags,out);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_wait(VkDevice d,uint32_t count,const VkFence *fences,VkBool32 all,uint64_t timeout){
    if(timeout_fence){timeout_fence--;return VK_TIMEOUT;}return original_wait(d,count,fences,all,timeout);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_submit(VkQueue q,uint32_t count,const VkSubmitInfo *info,VkFence fence){
    if(fail_submit){fail_submit--;return VK_ERROR_DEVICE_LOST;}return original_submit(q,count,info,fence);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_idle(VkDevice d){if(fail_idle){fail_idle--;return VK_ERROR_OUT_OF_HOST_MEMORY;}return original_idle(d);}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_invalidate(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges){
    assert(count==1 && ranges[0].offset==0 && ranges[0].size==VK_WHOLE_SIZE);invalidations++;return original_invalidate(d,count,ranges);
}
static VKAPI_ATTR VkResult VKAPI_CALL proxy_flush(VkDevice d,uint32_t count,const VkMappedMemoryRange *ranges){
    assert(count==1 && ranges[0].offset==0 && ranges[0].size==VK_WHOLE_SIZE);
    if(fail_flush){fail_flush--;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    flushes++;return original_flush(d,count,ranges);
}
static VKAPI_ATTR void VKAPI_CALL proxy_memory_properties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out){
    original_memory_properties(p,out);
    if(force_noncoherent)for(uint32_t i=0;i<out->memoryTypeCount;++i)out->memoryTypes[i].propertyFlags&=~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proxy_gdpa(VkDevice d,const char *name){
    PFN_vkVoidFunction p=original_gdpa(d,name);
#define LOAD_OBJECT(n) \
    if(!strcmp(name,"vkCreate" #n)){original_create_##n=(PFN_vkCreate##n)p;return (PFN_vkVoidFunction)proxy_create_##n;} \
    if(!strcmp(name,"vkDestroy" #n)){original_destroy_##n=(PFN_vkDestroy##n)p;return (PFN_vkVoidFunction)proxy_destroy_##n;}
    if(!strcmp(name,"vkCreateSampler")){original_create_Sampler=(PFN_vkCreateSampler)p;return (PFN_vkVoidFunction)proxy_sampler_override;}
    if(!strcmp(name,"vkCreateImageView")){original_create_ImageView=(PFN_vkCreateImageView)p;return (PFN_vkVoidFunction)proxy_imageview_override;}
    OBJECTS(LOAD_OBJECT)
#undef LOAD_OBJECT
#define HOOK(vk,slot,fn) if(!strcmp(name,"vk" #vk)){slot=(PFN_vk##vk)p;return (PFN_vkVoidFunction)fn;}
    HOOK(AllocateMemory,original_allocate,proxy_allocate)
    HOOK(FreeMemory,original_free,proxy_free)
    HOOK(MapMemory,original_map,proxy_map)
    HOOK(WaitForFences,original_wait,proxy_wait)
    HOOK(QueueSubmit,original_submit,proxy_submit)
    HOOK(CreateGraphicsPipelines,original_pipelines,proxy_pipelines)
    HOOK(DestroyPipeline,original_destroy_pipeline,proxy_destroy_pipeline)
    HOOK(AllocateDescriptorSets,original_descriptor_sets,proxy_descriptor_sets)
    HOOK(EndCommandBuffer,original_end,proxy_end)
    HOOK(ResetFences,original_reset,proxy_reset)
    HOOK(DeviceWaitIdle,original_idle,proxy_idle)
    HOOK(InvalidateMappedMemoryRanges,original_invalidate,proxy_invalidate)
    HOOK(FlushMappedMemoryRanges,original_flush,proxy_flush)
#undef HOOK
    return p;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proxy_gpa(VkInstance instance,const char *name){
    PFN_vkVoidFunction p=original_gpa(instance,name);
    if(!strcmp(name,"vkGetPhysicalDeviceFeatures")){original_features=(PFN_vkGetPhysicalDeviceFeatures)p;return (PFN_vkVoidFunction)proxy_features;}
    if(!strcmp(name,"vkGetDeviceProcAddr")){original_gdpa=(PFN_vkGetDeviceProcAddr)p;return (PFN_vkVoidFunction)proxy_gdpa;}
    if(!strcmp(name,"vkGetPhysicalDeviceFormatProperties")){original_format_properties=(PFN_vkGetPhysicalDeviceFormatProperties)p;return (PFN_vkVoidFunction)proxy_format_properties;}
    if(!strcmp(name,"vkGetPhysicalDeviceMemoryProperties")){original_memory_properties=(PFN_vkGetPhysicalDeviceMemoryProperties)p;return (PFN_vkVoidFunction)proxy_memory_properties;}
    return p;
}
static void no_live_handles(void){
#define CHECK_OBJECT(n) assert(!live_##n);
    OBJECTS(CHECK_OBJECT)
#undef CHECK_OBJECT
    assert(!live_memory && !live_Pipeline);
}
static unsigned exercise_faults(const struct fnv_vk_config *cfg){
    unsigned scenarios=0;VkResult reason;struct fnv_vk_backend *d;struct fnv_d3d9_shader_backend b;struct fnv_vk_stats s;
    fail_PipelineCache=1;d=(void*)1;
    assert(fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason)==FNV_VK_OOM && !d);no_live_handles();
    for(unsigned i=0;i<3;++i){
        if(i==0)fail_memory=1;else if(i==1)fail_ImageView=1;else fail_Fence=1;
        d=(void*)1;assert(fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason)==FNV_VK_OOM && !d);no_live_handles();scenarios++;
    }
    assert(!fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason));fnv_vk_backend_callbacks(d,&b);
    assert(!b.clear(d,0,NULL,1,0xff123456,0,0));assert(!fnv_vk_backend_flush(d));assert(!fnv_vk_backend_wait(d,10000000000ull));
    uintptr_t id;assert(!b.get_target(d,0,0,&id));b.release(d,id);unsigned char output[4096];memset(output,0xa5,sizeof(output));
    assert(!fnv_vk_backend_stats(d,&s));uint64_t before=s.allocated_bytes,completed=s.completions;
    timeout_fence=1;assert(fnv_vk_backend_read_color(d,id,output,sizeof(output),0)==FNV_VK_BUSY);
    for(unsigned i=0;i<sizeof(output);++i)assert(output[i]==0xa5);
    assert(!fnv_vk_backend_stats(d,&s) && s.pending_frames==1 && s.completions==completed && s.allocated_bytes>before);
    /* Drop the last public device reference while readback is in flight. */
    b.device_release(d);assert(fnv_vk_backend_destroy(d)==FNV_VK_BUSY);assert(!fnv_vk_backend_wait(d,10000000000ull));
    assert(!fnv_vk_backend_stats(d,&s) && !s.allocated_bytes && !s.live_surfaces);assert(!fnv_vk_backend_destroy(d));no_live_handles();scenarios++;
    force_noncoherent=1;assert(!fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason));force_noncoherent=0;fnv_vk_backend_callbacks(d,&b);
    assert(!b.clear(d,0,NULL,1,0xff123456,0,0));assert(!fnv_vk_backend_flush(d));assert(!fnv_vk_backend_wait(d,10000000000ull));
    assert(!b.get_target(d,0,0,&id));b.release(d,id);fail_map=1;
    assert(fnv_vk_backend_read_color(d,id,output,sizeof(output),10000000000ull)==FNV_VK_OOM);
    for(unsigned i=0;i<sizeof(output);++i)assert(output[i]==0xa5);
    assert(!fnv_vk_backend_read_color(d,id,output,sizeof(output),10000000000ull) && invalidations);
    for(unsigned i=0;i<1024;++i){uint32_t pixel;memcpy(&pixel,output+i*4,4);assert(pixel==0xff123456);}
    b.device_release(d);assert(!fnv_vk_backend_destroy(d));no_live_handles();scenarios+=2;
    for(unsigned i=0;i<2;++i){
        assert(!fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason));fnv_vk_backend_callbacks(d,&b);assert(!b.clear(d,0,NULL,1,0,0,0));
        if(i==0)fail_Framebuffer=1;else fail_submit=1;
        assert(fnv_vk_backend_flush(d)==(i==0?FNV_VK_OOM:FNV_VK_LOST));
        assert(!fnv_vk_backend_stats(d,&s) && !s.submissions && !s.completions);
        assert(fnv_vk_backend_poll(d)==FNV_VK_LOST && fnv_vk_backend_flush(d)==FNV_VK_LOST);
        b.device_release(d);
        if(i==1){fail_idle=1;assert(fnv_vk_backend_destroy(d)==FNV_VK_LOST && live_memory && live_Image);scenarios++;}
        assert(!fnv_vk_backend_destroy(d));no_live_handles();scenarios++;
    }
    return scenarios;
}
