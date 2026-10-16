#ifndef FNV_D3D9_VK_BACKEND_H
#define FNV_D3D9_VK_BACKEND_H
#include "d3d9_com.h"
#include <vulkan/vulkan_core.h>
struct fnv_vk_backend;
struct fnv_vk_config {
    uint32_t width,height,resource_capacity,frames,commands;
    uint64_t memory_budget;
    uint32_t auto_depth; /* D24S8, optional. Offscreen color is ARGB8. */
};
struct fnv_vk_stats {
    uint64_t allocated_bytes,submissions,completions,device_references;
    uint32_t live_surfaces,queued_commands,pending_frames;
    VkResult last_result;
    uint32_t live_buffers,buffer_versions,live_textures,texture_versions;
};
#define FNV_VK_INVALID 0x8876086cu
#define FNV_VK_OOM 0x8007000eu
#define FNV_VK_BUSY 0x8876021cu
#define FNV_VK_LOST 0x88760868u
/* Actual headless Vulkan 1.1 device and offscreen targets; no window/swapchain.
 * Loader must remain alive until destroy. One initial device reference is
 * transferred to the COM consumer via callbacks. Resource public references
 * retain their device; internal bindings/queued work do not create cycles.
 * No fake shader/draw provider: unsupported callback extensions remain null.
 * Backend methods serialize internally; no callbacks/reentry under its lock. */
uint32_t fnv_vk_backend_create(PFN_vkGetInstanceProcAddr,const struct fnv_vk_config *,struct fnv_vk_backend **,VkResult *failure);
void fnv_vk_backend_callbacks(struct fnv_vk_backend *,struct fnv_d3d9_shader_backend *);
uint32_t fnv_vk_backend_stats(struct fnv_vk_backend *,struct fnv_vk_stats *);
/* Clear queues owned commands; EndScene does not submit or observe fences.
 * flush never waits or implicitly retires frames: full ring returns BUSY.
 * poll only retires signaled fences. wait has a caller-supplied total timeout
 * and returns BUSY on timeout. All work uses one ordered graphics queue. */
uint32_t fnv_vk_backend_flush(struct fnv_vk_backend *);
uint32_t fnv_vk_backend_poll(struct fnv_vk_backend *);
uint32_t fnv_vk_backend_wait(struct fnv_vk_backend *,uint64_t timeout_ns);
/* Debug/offscreen readback: submits queued clears, waits submitted work, copies
 * a color surface into owned staging and waits its real fence. Output is BGRA8
 * tightly packed; unchanged on error/timeout. Temporary GPU resources remain
 * owned by the frame after timeout. Not a Present implementation. */
uint32_t fnv_vk_backend_read_color(struct fnv_vk_backend *,uintptr_t,void *,size_t,uint64_t timeout_ns);
/* Debug GPU transfer readback of an arbitrary VB/IB byte range. Each upload
 * installs an immutable VkBuffer version; pending transfers keep the previous
 * version alive through its real fence. Output unchanged on error/timeout. */
uint32_t fnv_vk_backend_read_buffer(struct fnv_vk_backend *,uintptr_t,uint32_t offset,uint32_t bytes,void *,size_t,uint64_t timeout_ns);
/* Diagnostic tightly packed RGBA8 readback of an uploaded mip. No sampling.
 * Uninitialized textures reject; output unchanged on error/timeout. */
uint32_t fnv_vk_backend_read_texture(struct fnv_vk_backend *,uintptr_t,uint32_t level,void *,size_t,uint64_t timeout_ns);
/* Caller prevents concurrent teardown. BUSY if public references or submitted
 * work remain. Unsubmitted commands can be discarded only after device refs
 * reach zero. A permanently lost device is cleaned without claiming completion. */
uint32_t fnv_vk_backend_destroy(struct fnv_vk_backend *);
#endif
