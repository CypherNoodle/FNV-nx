#ifndef FNV_D3D9_VK_HORIZON_H
#define FNV_D3D9_VK_HORIZON_H
#include "d3d9_vk_backend.h"
/* nwindow is a valid, initialized libnx NWindow*, retained by caller until
 * backend destruction succeeds. No service/window ownership is transferred.
 * Caller config must match its dimensions; this does not resize the window. */
uint32_t fnv_vk_backend_create_horizon(PFN_vkGetInstanceProcAddr,const struct fnv_vk_config *,void *nwindow,uint32_t vsync,struct fnv_vk_backend **,VkResult *failure);
#endif
