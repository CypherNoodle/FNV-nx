/* Native WSI only. No CPU readback or guest window handles on this path. */
static uint32_t create_wsi(struct fnv_vk_backend *d,uint32_t vsync){
    VkSurfaceCapabilitiesKHR caps;uint32_t hr=result(d,d->v.GetPhysicalDeviceSurfaceCapabilitiesKHR(d->physical,d->wsi.surface,&caps));if(hr)return hr;
    VkExtent2D extent={d->config.width,d->config.height};
    if((caps.currentExtent.width!=UINT32_MAX && (caps.currentExtent.width!=extent.width || caps.currentExtent.height!=extent.height)) ||
       extent.width<caps.minImageExtent.width || extent.height<caps.minImageExtent.height || extent.width>caps.maxImageExtent.width || extent.height>caps.maxImageExtent.height ||
       !(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT) || !(caps.supportedTransforms&VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ||
       !(caps.supportedCompositeAlpha&VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) || !caps.maxImageArrayLayers)return 0x8876086au;
    uint32_t count=0;hr=result(d,d->v.GetPhysicalDeviceSurfaceFormatsKHR(d->physical,d->wsi.surface,&count,NULL));if(hr)return hr;
    if(!count || count>64)return 0x8876086au;
    VkSurfaceFormatKHR formats[64];hr=result(d,d->v.GetPhysicalDeviceSurfaceFormatsKHR(d->physical,d->wsi.surface,&count,formats));if(hr)return hr;
    int format_ok=0;for(uint32_t i=0;i<count;++i)if((formats[i].format==VK_FORMAT_B8G8R8A8_UNORM || (count==1 && formats[i].format==VK_FORMAT_UNDEFINED)) && formats[i].colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)format_ok=1;
    if(!format_ok)return 0x8876086au;
    count=0;hr=result(d,d->v.GetPhysicalDeviceSurfacePresentModesKHR(d->physical,d->wsi.surface,&count,NULL));if(hr)return hr;
    if(!count || count>32)return 0x8876086au;
    VkPresentModeKHR modes[32],mode=vsync?VK_PRESENT_MODE_FIFO_KHR:VK_PRESENT_MODE_IMMEDIATE_KHR;
    hr=result(d,d->v.GetPhysicalDeviceSurfacePresentModesKHR(d->physical,d->wsi.surface,&count,modes));if(hr)return hr;
    int mode_ok=0;for(uint32_t i=0;i<count;++i)if(modes[i]==mode)mode_ok=1;
    if(!mode_ok)return 0x8876086au;
    uint32_t images=caps.minImageCount>3?caps.minImageCount:3;if(caps.maxImageCount && images>caps.maxImageCount)images=caps.maxImageCount;
    if(!images || images<caps.minImageCount || images>8)return 0x8876086au;
    VkSwapchainCreateInfoKHR info={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,.surface=d->wsi.surface,.minImageCount=images,
        .imageFormat=VK_FORMAT_B8G8R8A8_UNORM,.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,.imageExtent=extent,.imageArrayLayers=1,
        .imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,.preTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,.presentMode=mode,.clipped=VK_TRUE};
    VkSwapchainKHR chain=VK_NULL_HANDLE;hr=result(d,d->v.CreateSwapchainKHR(d->device,&info,NULL,&chain));if(hr)return hr;d->wsi.chain=chain;d->wsi.mode=mode;
    count=0;hr=result(d,d->v.GetSwapchainImagesKHR(d->device,chain,&count,NULL));if(hr)return hr;
    if(!count || count>8)return 0x8876086au;
    VkImage handles[8];hr=result(d,d->v.GetSwapchainImagesKHR(d->device,chain,&count,handles));if(hr)return hr;d->wsi.count=count;
    VkSemaphoreCreateInfo sem={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    for(uint32_t i=0;i<count;++i){struct wsi_image *image=d->wsi.images+i;image->image=handles[i];VkSemaphore s=VK_NULL_HANDLE;VkFence f=VK_NULL_HANDLE;
        hr=result(d,d->v.CreateSemaphore(d->device,&sem,NULL,&s));if(hr)return hr;image->ready=s;
        hr=result(d,d->v.CreateFence(d->device,&fence,NULL,&f));if(hr)return hr;image->presented=f;}
    for(uint32_t i=0;i<d->config.frames;++i){VkSemaphore s=VK_NULL_HANDLE;VkFence f=VK_NULL_HANDLE;
        hr=result(d,d->v.CreateSemaphore(d->device,&sem,NULL,&s));if(hr)return hr;d->frames[i].acquired=s;
        hr=result(d,d->v.CreateFence(d->device,&fence,NULL,&f));if(hr)return hr;d->frames[i].acquire_fence=f;}
    return 0;
}
/* Called only after device idle, but that alone does not prove present done.
 * Present fences cover WSI semaphore use. Unknown completion retains context. */
static uint32_t destroy_wsi(struct fnv_vk_backend *d,int device_lost){
    if(!device_lost){
        for(uint32_t i=0;i<d->wsi.count;++i)if(d->wsi.images[i].pending){
            VkResult r=d->v.GetFenceStatus(d->device,d->wsi.images[i].presented);if(r==VK_NOT_READY)return FNV_VK_BUSY;if(r)return result(d,r);}
        for(uint32_t i=0;i<d->config.frames;++i)if(d->frames[i].acquire_pending){
            VkResult r=d->v.GetFenceStatus(d->device,d->frames[i].acquire_fence);if(r==VK_NOT_READY)return FNV_VK_BUSY;if(r)return result(d,r);}
    }
    for(uint32_t i=0;i<d->wsi.count;++i){struct wsi_image *image=d->wsi.images+i;
        if(image->ready)d->v.DestroySemaphore(d->device,image->ready,NULL);
        if(image->presented)d->v.DestroyFence(d->device,image->presented,NULL);}
    for(uint32_t i=0;i<d->config.frames;++i){struct frame *f=d->frames+i;
        if(f->acquired)d->v.DestroySemaphore(d->device,f->acquired,NULL);
        if(f->acquire_fence)d->v.DestroyFence(d->device,f->acquire_fence,NULL);}
    if(d->wsi.chain)d->v.DestroySwapchainKHR(d->device,d->wsi.chain,NULL);
    return 0;
}
static uint32_t present_impl(struct fnv_vk_backend *d,uint64_t timeout,int blocking){
    if(!d || timeout==UINT64_MAX)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;struct frame *f=NULL;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(!d->wsi.chain || d->in_scene)goto done;
    if(blocking && d->count){hr=wait_for_frame_locked(d,timeout);if(hr)goto done;}
    hr=flush_locked(d);if(hr)goto done;
    struct surface *back=find(d,d->back);if(!back || back->layout==VK_IMAGE_LAYOUT_UNDEFINED){hr=FNV_VK_INVALID;goto done;}
    if(blocking){hr=wait_for_frame_locked(d,timeout);if(hr)goto done;}
    for(uint32_t i=0;i<d->config.frames;++i)if(!d->frames[i].pending){f=d->frames+i;break;}
    if(!f){hr=FNV_VK_BUSY;goto done;}
    if(f->acquire_pending){
        /* A render fence proves consumption of the semaphore, not the host
         * acquisition fence signal. Observe that fence before resetting it. */
        hr=result(d,d->v.WaitForFences(d->device,1,&f->acquire_fence,VK_TRUE,timeout));if(hr)goto done;
        hr=result(d,d->v.ResetFences(d->device,1,&f->acquire_fence));if(hr){d->lost=1;goto done;}f->acquire_pending=0;
    }
    uint32_t index=UINT32_MAX;VkResult r=d->v.AcquireNextImageKHR(d->device,d->wsi.chain,timeout,f->acquired,f->acquire_fence,&index);
    if(r!=VK_SUCCESS && r!=VK_SUBOPTIMAL_KHR){if(r==VK_TIMEOUT || r==VK_NOT_READY)d->wsi.timeouts++;hr=result(d,r);goto done;}
    f->acquire_pending=1;
    if(index>=d->wsi.count){d->lost=1;hr=FNV_VK_LOST;goto done;}
    struct wsi_image *image=d->wsi.images+index;
    /* Acquisition permits reuse of the image semaphore only after its wait.
     * Wait for the maintenance fence separately before reusing that fence. */
    if(image->pending){r=d->v.WaitForFences(d->device,1,&image->presented,VK_TRUE,timeout);
        if(r){d->lost=1;(void)result(d,r);hr=FNV_VK_LOST;goto done;}
        r=d->v.ResetFences(d->device,1,&image->presented);if(r){d->lost=1;hr=result(d,r);goto done;}image->pending=0;}
    hr=begin_frame(d,&f);if(hr){d->lost=1;goto done;}
    f->read_surface=d->back;pin(d,d->back,1);barrier(d,f->cmd,back);
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image->image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&b);
    VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.extent={back->width,back->height,1}};
    d->v.CmdCopyImage(f->cmd,back->image,VK_IMAGE_LAYOUT_GENERAL,image->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=0;b.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&b);
    hr=result(d,d->v.EndCommandBuffer(f->cmd));if(hr){d->lost=1;goto done;}
    hr=result(d,d->v.ResetFences(d->device,1,&f->fence));if(hr){d->lost=1;goto done;}
    VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=1,.pWaitSemaphores=&f->acquired,.pWaitDstStageMask=&stage,
        .commandBufferCount=1,.pCommandBuffers=&f->cmd,.signalSemaphoreCount=1,.pSignalSemaphores=&image->ready};
    d->queue_touched=1;hr=result(d,d->v.QueueSubmit(d->queue,1,&submit,f->fence));if(hr){d->lost=1;goto done;}
    f->pending=1;d->submissions++;
    VkSwapchainPresentFenceInfoEXT fence={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,.swapchainCount=1,.pFences=&image->presented};
    VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.pNext=&fence,.waitSemaphoreCount=1,.pWaitSemaphores=&image->ready,
        .swapchainCount=1,.pSwapchains=&d->wsi.chain,.pImageIndices=&index};
    r=d->v.QueuePresentKHR(d->queue,&present);
    /* Error may have enqueued the wait. Keep the present fence/context even
     * for uncertain completion; teardown may return BUSY until signaled. */
    image->pending=1;
    if(r<0)d->lost=1;
    else d->wsi.presents++;
    hr=r==VK_SUBOPTIMAL_KHR?0:result(d,r);
done:
    if(blocking && hr==FNV_VK_BUSY){d->lost=1;hr=FNV_VK_LOST;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_present(struct fnv_vk_backend *d,uint64_t timeout){return present_impl(d,timeout,0);}
uint32_t fnv_vk_backend_present_sync(struct fnv_vk_backend *d,uint64_t timeout){
    if(!timeout)return FNV_VK_INVALID;
    return present_impl(d,timeout,1);
}
