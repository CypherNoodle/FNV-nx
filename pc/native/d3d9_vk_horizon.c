#include "d3d9_vk_horizon.h"
#include <switch.h>
#include <vulkan/vulkan_vi.h>
static VkResult horizon_surface(void *window,VkInstance instance,PFN_vkGetInstanceProcAddr gp,VkSurfaceKHR *out){
    *out=VK_NULL_HANDLE;
    PFN_vkCreateViSurfaceNN create=(PFN_vkCreateViSurfaceNN)gp(instance,"vkCreateViSurfaceNN");
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkViSurfaceCreateInfoNN info={.sType=VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN,.window=window};
    return create(instance,&info,NULL,out);
}
uint32_t fnv_vk_backend_create_horizon(PFN_vkGetInstanceProcAddr gp,const struct fnv_vk_config *cfg,void *nwindow,uint32_t vsync,struct fnv_vk_backend **out,VkResult *failure){
    if(!nwindow || !nwindowIsValid(nwindow)){if(out)*out=NULL;if(failure)*failure=VK_SUCCESS;return FNV_VK_INVALID;}
    struct fnv_vk_window window={VK_NN_VI_SURFACE_EXTENSION_NAME,nwindow,horizon_surface,vsync};
    return fnv_vk_backend_create_window(gp,cfg,&window,out,failure);
}
