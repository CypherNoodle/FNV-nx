/* Test-only dispatch proxy. Real Vulkan handles/work; deterministic injected
 * API failures verify rollback and lifetime without requiring driver faults. */
static PFN_vkGetInstanceProcAddr original_gpa;
static PFN_vkGetDeviceProcAddr original_gdpa;
static unsigned fail_memory,fail_map,timeout_fence,fail_submit,fail_idle,invalidations;
static int force_noncoherent;
#define OBJECTS(X) X(Image) X(ImageView) X(Buffer) X(RenderPass) X(Framebuffer) X(CommandPool) X(Fence)
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
static PFN_vkAllocateMemory original_allocate;
static PFN_vkFreeMemory original_free;
static PFN_vkMapMemory original_map;
static PFN_vkWaitForFences original_wait;
static PFN_vkQueueSubmit original_submit;
static PFN_vkDeviceWaitIdle original_idle;
static PFN_vkInvalidateMappedMemoryRanges original_invalidate;
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
static VKAPI_ATTR void VKAPI_CALL proxy_memory_properties(VkPhysicalDevice p,VkPhysicalDeviceMemoryProperties *out){
    original_memory_properties(p,out);
    if(force_noncoherent)for(uint32_t i=0;i<out->memoryTypeCount;++i)out->memoryTypes[i].propertyFlags&=~VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proxy_gdpa(VkDevice d,const char *name){
    PFN_vkVoidFunction p=original_gdpa(d,name);
#define LOAD_OBJECT(n) \
    if(!strcmp(name,"vkCreate" #n)){original_create_##n=(PFN_vkCreate##n)p;return (PFN_vkVoidFunction)proxy_create_##n;} \
    if(!strcmp(name,"vkDestroy" #n)){original_destroy_##n=(PFN_vkDestroy##n)p;return (PFN_vkVoidFunction)proxy_destroy_##n;}
    OBJECTS(LOAD_OBJECT)
#undef LOAD_OBJECT
#define HOOK(vk,slot,fn) if(!strcmp(name,"vk" #vk)){slot=(PFN_vk##vk)p;return (PFN_vkVoidFunction)fn;}
    HOOK(AllocateMemory,original_allocate,proxy_allocate)
    HOOK(FreeMemory,original_free,proxy_free)
    HOOK(MapMemory,original_map,proxy_map)
    HOOK(WaitForFences,original_wait,proxy_wait)
    HOOK(QueueSubmit,original_submit,proxy_submit)
    HOOK(DeviceWaitIdle,original_idle,proxy_idle)
    HOOK(InvalidateMappedMemoryRanges,original_invalidate,proxy_invalidate)
#undef HOOK
    return p;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL proxy_gpa(VkInstance instance,const char *name){
    PFN_vkVoidFunction p=original_gpa(instance,name);
    if(!strcmp(name,"vkGetDeviceProcAddr")){original_gdpa=(PFN_vkGetDeviceProcAddr)p;return (PFN_vkVoidFunction)proxy_gdpa;}
    if(!strcmp(name,"vkGetPhysicalDeviceMemoryProperties")){original_memory_properties=(PFN_vkGetPhysicalDeviceMemoryProperties)p;return (PFN_vkVoidFunction)proxy_memory_properties;}
    return p;
}
static void no_live_handles(void){
#define CHECK_OBJECT(n) assert(!live_##n);
    OBJECTS(CHECK_OBJECT)
#undef CHECK_OBJECT
    assert(!live_memory);
}
static unsigned exercise_faults(const struct fnv_vk_config *cfg){
    unsigned scenarios=0;VkResult reason;struct fnv_vk_backend *d;struct fnv_d3d9_shader_backend b;struct fnv_vk_stats s;
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
