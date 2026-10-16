/* Real Win32 Vulkan WSI oracle for the platform-independent native backend.
 * Hidden owned window, no Windows D3D9 device and no game code. */
#include <windows.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9_vk_backend.h"
#include <vulkan/vulkan_win32.h>
#include "vk_backend_fault_proxy.h"
static PFN_vkCreateSwapchainKHR real_chain;
static PFN_vkDestroySwapchainKHR real_destroy_chain;
static PFN_vkCreateSemaphore real_sem;
static PFN_vkDestroySemaphore real_destroy_sem;
static PFN_vkAcquireNextImageKHR real_acquire;
static PFN_vkQueuePresentKHR real_present;
static PFN_vkCmdCopyImage real_copy;
static PFN_vkDestroySurfaceKHR real_destroy_surface;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR real_caps;
static PFN_vkGetPhysicalDeviceFeatures2 real_features;
static unsigned chains,semaphores,surfaces,acquires,presents,copies,deny_cap,deny_feature,fail_chain,fail_sem,acquire_timeout,delay_present;
static VkResult acquire_error;
static VkQueue delayed_queue;static VkSwapchainKHR delayed_chain;static VkSemaphore delayed_sem;static VkFence delayed_fence;static uint32_t delayed_index;
static VKAPI_ATTR VkResult VKAPI_CALL create_chain(VkDevice d,const VkSwapchainCreateInfoKHR *i,const VkAllocationCallbacks *a,VkSwapchainKHR *out){
    assert(i->imageUsage==VK_IMAGE_USAGE_TRANSFER_DST_BIT && i->imageFormat==VK_FORMAT_B8G8R8A8_UNORM && i->oldSwapchain==VK_NULL_HANDLE);
    if(fail_chain){fail_chain--;*out=(VkSwapchainKHR)(uintptr_t)0xdeadbad;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    VkResult r=real_chain(d,i,a,out);if(!r)chains++;return r;
}
static VKAPI_ATTR void VKAPI_CALL destroy_chain(VkDevice d,VkSwapchainKHR s,const VkAllocationCallbacks *a){assert(chains);chains--;real_destroy_chain(d,s,a);}
static VKAPI_ATTR VkResult VKAPI_CALL create_sem(VkDevice d,const VkSemaphoreCreateInfo *i,const VkAllocationCallbacks *a,VkSemaphore *out){
    if(fail_sem && !--fail_sem){*out=(VkSemaphore)(uintptr_t)0xdeadbad;return VK_ERROR_OUT_OF_HOST_MEMORY;}
    VkResult r=real_sem(d,i,a,out);if(!r)semaphores++;return r;
}
static VKAPI_ATTR void VKAPI_CALL destroy_sem(VkDevice d,VkSemaphore s,const VkAllocationCallbacks *a){assert(semaphores);semaphores--;real_destroy_sem(d,s,a);}
static VKAPI_ATTR VkResult VKAPI_CALL acquire(VkDevice d,VkSwapchainKHR s,uint64_t timeout,VkSemaphore sem,VkFence fence,uint32_t *index){
    assert(sem && fence);acquires++;if(acquire_timeout){acquire_timeout--;return VK_TIMEOUT;}if(acquire_error)return acquire_error;return real_acquire(d,s,timeout,sem,fence,index);
}
static VKAPI_ATTR VkResult VKAPI_CALL present(VkQueue q,const VkPresentInfoKHR *p){
    assert(p->swapchainCount==1 && p->waitSemaphoreCount==1 && p->pNext);
    const VkSwapchainPresentFenceInfoEXT *f=p->pNext;assert(f->sType==VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT && f->swapchainCount==1 && f->pFences[0]);
    if(delay_present){delay_present--;delayed_queue=q;delayed_chain=p->pSwapchains[0];delayed_sem=p->pWaitSemaphores[0];delayed_fence=f->pFences[0];delayed_index=p->pImageIndices[0];return VK_ERROR_OUT_OF_HOST_MEMORY;}
    presents++;return real_present(q,p);
}
static VKAPI_ATTR void VKAPI_CALL copy_image(VkCommandBuffer c,VkImage src,VkImageLayout sl,VkImage dst,VkImageLayout dl,uint32_t n,const VkImageCopy *r){
    assert(n==1 && src!=dst && sl==VK_IMAGE_LAYOUT_GENERAL && dl==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && r->extent.width==32 && r->extent.height==32 && r->extent.depth==1);
    copies++;real_copy(c,src,sl,dst,dl,n,r);
}
static VKAPI_ATTR void VKAPI_CALL destroy_surface(VkInstance i,VkSurfaceKHR s,const VkAllocationCallbacks *a){assert(surfaces);surfaces--;real_destroy_surface(i,s,a);}
static VKAPI_ATTR VkResult VKAPI_CALL caps(VkPhysicalDevice p,VkSurfaceKHR s,VkSurfaceCapabilitiesKHR *c){VkResult r=real_caps(p,s,c);
    if(!r && deny_cap)c->supportedUsageFlags&=~VK_IMAGE_USAGE_TRANSFER_DST_BIT;return r;}
static VKAPI_ATTR void VKAPI_CALL features(VkPhysicalDevice p,VkPhysicalDeviceFeatures2 *f){real_features(p,f);if(deny_feature)((VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*)f->pNext)->swapchainMaintenance1=VK_FALSE;}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wsi_gdpa(VkDevice d,const char *name){
    PFN_vkVoidFunction p=proxy_gdpa(d,name);
#define HOOK(n,slot,fn) if(!strcmp(name,"vk" #n)){slot=(PFN_vk##n)p;return (PFN_vkVoidFunction)fn;}
    HOOK(CreateSwapchainKHR,real_chain,create_chain) HOOK(DestroySwapchainKHR,real_destroy_chain,destroy_chain)
    HOOK(CreateSemaphore,real_sem,create_sem) HOOK(DestroySemaphore,real_destroy_sem,destroy_sem)
    HOOK(AcquireNextImageKHR,real_acquire,acquire) HOOK(QueuePresentKHR,real_present,present) HOOK(CmdCopyImage,real_copy,copy_image)
#undef HOOK
    return p;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL wsi_gpa(VkInstance instance,const char *name){
    PFN_vkVoidFunction p=proxy_gpa(instance,name);
    if(!strcmp(name,"vkGetDeviceProcAddr"))return (PFN_vkVoidFunction)wsi_gdpa;
    if(!strcmp(name,"vkDestroySurfaceKHR")){real_destroy_surface=(PFN_vkDestroySurfaceKHR)p;return (PFN_vkVoidFunction)destroy_surface;}
    if(!strcmp(name,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")){real_caps=(PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)p;return (PFN_vkVoidFunction)caps;}
    if(!strcmp(name,"vkGetPhysicalDeviceFeatures2")){real_features=(PFN_vkGetPhysicalDeviceFeatures2)p;return (PFN_vkVoidFunction)features;}
    return p;
}
static VkResult window_surface(void *hwnd,VkInstance instance,PFN_vkGetInstanceProcAddr gp,VkSurfaceKHR *out){
    PFN_vkCreateWin32SurfaceKHR create=(PFN_vkCreateWin32SurfaceKHR)gp(instance,"vkCreateWin32SurfaceKHR");assert(create);
    VkWin32SurfaceCreateInfoKHR info={.sType=VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,.hinstance=GetModuleHandleW(NULL),.hwnd=hwnd};
    VkResult r=create(instance,&info,NULL,out);if(!r)surfaces++;return r;
}
static void check_empty(void){no_live_handles();assert(!surfaces && !semaphores && !chains);}
static void close_backend(struct fnv_vk_backend *d,struct fnv_d3d9_shader_backend *b){
    assert(!fnv_vk_backend_wait(d,10000000000ull));b->device_release(d);
    uint32_t hr=FNV_VK_BUSY;for(unsigned i=0;i<100 && hr==FNV_VK_BUSY;++i){hr=fnv_vk_backend_destroy(d);if(hr==FNV_VK_BUSY)Sleep(1);}assert(!hr);check_empty();
}
static void clear_ready(struct fnv_vk_backend *d,struct fnv_d3d9_shader_backend *b,uint32_t color){
    assert(!b->clear(d,0,NULL,1,color,0,0));assert(!fnv_vk_backend_flush(d));assert(!fnv_vk_backend_wait(d,10000000000ull));
}
int main(void){
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC symbol=GetProcAddress(loader,"vkGetInstanceProcAddr");memcpy(&original_gpa,&symbol,sizeof(original_gpa));assert(original_gpa);
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVNativeWSIProbe";assert(RegisterClassW(&wc));
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"FNV WSI test",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(hwnd);
    RECT client;assert(GetClientRect(hwnd,&client) && client.right==32 && client.bottom==32);
    struct fnv_vk_config cfg={32,32,16,2,4,8u<<20,1};struct fnv_vk_window window={VK_KHR_WIN32_SURFACE_EXTENSION_NAME,hwnd,window_surface,1};
    struct fnv_vk_backend *d;struct fnv_d3d9_shader_backend b;VkResult failure;
    for(unsigned i=0;i<5;++i){if(i==0)deny_cap=1;else if(i==1)deny_feature=1;else if(i==2)fail_chain=1;else fail_sem=i==3?1:3;
        d=(void*)1;uint32_t expected=i==0?0x8876086au:i==1?FNV_VK_INVALID:FNV_VK_OOM;
        assert(fnv_vk_backend_create_window(wsi_gpa,&cfg,&window,&d,&failure)==expected && !d);assert(!fail_chain && !fail_sem);deny_cap=deny_feature=0;check_empty();}
    unsigned expected_presents=0;
    for(unsigned sync=0;sync<2;++sync){window.vsync=sync;uint32_t created=fnv_vk_backend_create_window(wsi_gpa,&cfg,&window,&d,&failure);
        if(created)fprintf(stderr,"WSI create failed: hr=%08x vk=%d sync=%u\n",created,(int)failure,sync);
        assert(!created);fnv_vk_backend_callbacks(d,&b);
        assert(fnv_vk_backend_present(d,0)==FNV_VK_INVALID && fnv_vk_backend_present(d,UINT64_MAX)==FNV_VK_INVALID);clear_ready(d,&b,0xff123456);
        assert(!b.scene(d,1));assert(fnv_vk_backend_present(d,0)==FNV_VK_INVALID);assert(!b.scene(d,0));
        unsigned char pixels[4096];uintptr_t back;assert(!b.get_target(d,0,0,&back));b.release(d,back);
        assert(!fnv_vk_backend_read_color(d,back,pixels,sizeof(pixels),10000000000ull));
        for(unsigned i=0;i<1024;++i){uint32_t v;memcpy(&v,pixels+i*4,4);assert(v==0xff123456);}
        acquire_timeout=1;assert(fnv_vk_backend_present(d,0)==FNV_VK_BUSY);
        for(unsigned i=0;i<6;++i){assert(!fnv_vk_backend_present(d,1000000000ull));expected_presents++;
            assert(!fnv_vk_backend_wait(d,10000000000ull));}
        unsigned prior_acquires=acquires;timeout_fence=1;assert(fnv_vk_backend_present(d,0)==FNV_VK_BUSY && acquires==prior_acquires && !timeout_fence);
        assert(!fnv_vk_backend_present(d,1000000000ull));assert(!fnv_vk_backend_present(d,1000000000ull));expected_presents+=2;
        unsigned before=acquires;assert(fnv_vk_backend_present(d,0)==FNV_VK_BUSY && acquires==before);
        struct fnv_vk_stats stats;assert(!fnv_vk_backend_stats(d,&stats) && stats.presentation_requests==8 && stats.acquire_timeouts==1 && stats.swapchain_images>=2 && stats.present_mode==(uint32_t)(sync?VK_PRESENT_MODE_FIFO_KHR:VK_PRESENT_MODE_IMMEDIATE_KHR));
        close_backend(d,&b);
    }
    /* A failed QueuePresent may not have queued anything. Keep all uncertain
     * WSI resources until the test later submits that exact pending request. */
    assert(!fnv_vk_backend_create_window(wsi_gpa,&cfg,&window,&d,&failure));fnv_vk_backend_callbacks(d,&b);clear_ready(d,&b,0xffabcdef);
    delay_present=1;assert(fnv_vk_backend_present(d,1000000000ull)==FNV_VK_OOM);assert(fnv_vk_backend_present(d,0)==FNV_VK_LOST);
    b.device_release(d);assert(fnv_vk_backend_destroy(d)==FNV_VK_BUSY && chains && semaphores && live_memory);
    VkSwapchainPresentFenceInfoEXT fence={.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,.swapchainCount=1,.pFences=&delayed_fence};
    VkPresentInfoKHR info={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.pNext=&fence,.waitSemaphoreCount=1,.pWaitSemaphores=&delayed_sem,.swapchainCount=1,.pSwapchains=&delayed_chain,.pImageIndices=&delayed_index};
    VkResult r=real_present(delayed_queue,&info);assert(r==VK_SUCCESS || r==VK_SUBOPTIMAL_KHR);
    uint32_t hr=FNV_VK_BUSY;for(unsigned i=0;i<100 && hr==FNV_VK_BUSY;++i){hr=fnv_vk_backend_destroy(d);if(hr==FNV_VK_BUSY)Sleep(1);}assert(!hr);check_empty();
    assert(presents==expected_presents && copies==expected_presents+1);
    for(unsigned fault=0;fault<7;++fault){
        assert(!fnv_vk_backend_create_window(wsi_gpa,&cfg,&window,&d,&failure));fnv_vk_backend_callbacks(d,&b);clear_ready(d,&b,0xffabcdef);
        if(fault==0)acquire_error=VK_ERROR_OUT_OF_DATE_KHR;else if(fault==1)acquire_error=VK_ERROR_SURFACE_LOST_KHR;
        else if(fault==2)fail_end=1;else if(fault==3)fail_reset=1;else if(fault==4)fail_submit=1;
        else if(fault==5)acquire_timeout=1;
        else {assert(!fnv_vk_backend_present(d,1000000000ull));assert(!fnv_vk_backend_present(d,1000000000ull));timeout_fence=1;}
        uint32_t expected=fault==2 || fault==3?FNV_VK_OOM:FNV_VK_LOST;
        assert((fault>=5?b.present(d,1000000000ull):fnv_vk_backend_present(d,1000000000ull))==expected);acquire_error=VK_SUCCESS;
        assert(!fail_end && !fail_reset && !fail_submit && !acquire_timeout && !timeout_fence && fnv_vk_backend_poll(d)==FNV_VK_LOST);
        b.device_release(d);uint32_t closed=FNV_VK_BUSY;
        for(unsigned i=0;i<100 && closed==FNV_VK_BUSY;++i){closed=fnv_vk_backend_destroy(d);if(closed==FNV_VK_BUSY)Sleep(1);}assert(!closed);check_empty();
    }
    DestroyWindow(hwnd);UnregisterClassW(wc.lpszClassName,wc.hInstance);FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"actual_wsi_presents\":%u,\"copy_commands\":%u,\"source_pixels_checked\":2048,\"setup_faults\":5,\"runtime_faults\":7,\"acquire_timeouts\":2,\"full_ring_checked\":true,\"unknown_present_quarantined\":true,\"all_gpu_handles_released\":true,\"game_frame_rendered\":false,\"switch_hardware_tested\":false}\n",presents,copies);return 0;
}
