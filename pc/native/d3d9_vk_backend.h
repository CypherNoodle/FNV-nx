#ifndef FNV_D3D9_VK_BACKEND_H
#define FNV_D3D9_VK_BACKEND_H
#include "d3d9_com.h"
#include "d3d9_uniform_state.h"
#include "d3d9_vk_state.h"
#include <vulkan/vulkan_core.h>
struct fnv_vk_backend;
struct fnv_vk_prepared_program;
/* Trusted offline pair with optional stage variants. Exclusive init only;
 * no replacement of an existing original pair. Later failure may be partial. */
uint32_t fnv_vk_backend_prepare_variant(struct fnv_vk_backend *,const struct fnv_vk_prepared_program *);
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
    uint32_t live_shaders,live_declarations,shader_catalog_entries,linked_programs;
    uint64_t draws;
    uint64_t presentation_requests,acquire_timeouts;
    uint32_t swapchain_images,present_mode; /* Vulkan mode; valid with images. */
    uint64_t resets;
    uint64_t draw_waits; /* Ring-pressure fence waits, not one wait per draw. */
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
/* Exclusive initialization, before COM construction or exposing the backbuffer.
 * Select ARGB8 (21, default) or XRGB8 (22) for the implicit presentation surface.
 * Storage remains BGRA8; X is unspecified and WSI uses opaque composition.
 * Does not add XRGB render textures or alter explicit offscreen surfaces. */
uint32_t fnv_vk_backend_presentation_format(struct fnv_vk_backend *,uint32_t);
/* Optional native WSI. Caller owns the platform window until backend teardown;
 * backend owns a successfully returned Vulkan surface. EXT maintenance1 is
 * required so presentation completion is observed before semaphore teardown.
 * One graphics/present queue, fixed extent, BGRA8, opaque, FIFO or IMMEDIATE.
 * Platform callback must return NULL surface on failure; runs only at create. */
struct fnv_vk_window {
    const char *extension;
    void *context;
    VkResult (*create_surface)(void *,VkInstance,PFN_vkGetInstanceProcAddr,VkSurfaceKHR *);
    uint32_t vsync;
};
uint32_t fnv_vk_backend_create_window(PFN_vkGetInstanceProcAddr,const struct fnv_vk_config *,const struct fnv_vk_window *,struct fnv_vk_backend **,VkResult *failure);
/* Host presentation of the implicit backbuffer, outside Begin/EndScene.
 * No CPU pixel readback, scaling, rects, guest HWND, Reset or automatic retry.
 * Each WSI wait has the explicit finite timeout (UINT64_MAX rejects); the
 * full command ring returns BUSY before
 * acquisition. Caller poll/waits then retries. Surface loss/out-of-date stop
 * this device (LOST), never recycle uncertain presentation synchronization. */
uint32_t fnv_vk_backend_present(struct fnv_vk_backend *,uint64_t acquire_timeout_ns);
/* Blocking DISCARD variant for the D3D9 callback: wait for a free command
 * slot, then acquire/present. Each wait uses this finite host watchdog.
 * A timeout stops this device and returns LOST; no spurious WASSTILLDRAWING.
 * Does not wait for scanout or force all frames to complete on every call. */
uint32_t fnv_vk_backend_present_sync(struct fnv_vk_backend *,uint64_t wait_timeout_ns);
/* Same dimensions/formats/presentation mode, healthy device only. Rebuild
 * implicit targets transactionally; wait submitted renders, clear state/banks,
 * retain managed buffers/textures and shader/declaration objects. Live public
 * DEFAULT resources/implicit targets reject. Lost-device/WSI recovery pending. */
uint32_t fnv_vk_backend_reset(struct fnv_vk_backend *,uint64_t wait_timeout_ns);
void fnv_vk_backend_callbacks(struct fnv_vk_backend *,struct fnv_d3d9_shader_backend *);
/* Conservative 304-byte packed x86 D3DCAPS9 snapshot. This is an incomplete
 * D3D9 implementation, not general shader-model conformance. Shader versions
 * and fixed-function stages remain zero even with a prepared shader catalog.
 * Invalid/lost inputs leave output untouched. No allocations or guest calls. */
uint32_t fnv_vk_backend_device_caps(struct fnv_vk_backend *,void *,uint32_t bytes);
uint32_t fnv_vk_backend_stats(struct fnv_vk_backend *,struct fnv_vk_stats *);
/* Optional finite draw backpressure watchdog, set before the first submission.
 * Zero (default) preserves nonblocking BUSY. Otherwise buffered, fixed and UP
 * draws wait for any completed frame only when their ring is full, sharing a
 * total deadline from the first pressure wait in that draw. Timeout loses the
 * device; uncertain GPU resources remain retained. UINT64_MAX rejects. */
uint32_t fnv_vk_backend_draw_wait(struct fnv_vk_backend *,uint64_t timeout_ns);
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
/* Diagnostic readback: RGBA8 for converted textures, tightly packed BC blocks
 * for DXT1/3/5. This is not a D3D9 application method or decoder. */
uint32_t fnv_vk_backend_read_texture(struct fnv_vk_backend *,uintptr_t,uint32_t level,void *,size_t,uint64_t timeout_ns);
/* Trusted host-side offline catalog: copy exact D3D9 tokens, prevalidated and
 * linked SPIR-V with entry main, and its reflected uniform plan. No runtime
 * translation or guest-provided SPIR-V. Basic header/stage checks are not a
 * substitute for spirv-val, reflection or pair-interface validation offline.
 * Duplicate stage/tokens reject; copies remain owned until backend destroy. */
uint32_t fnv_vk_backend_register_shader(struct fnv_vk_backend *,enum fnv_d3d9_stage,const void *code,uint32_t code_bytes,const void *spirv,uint32_t spirv_bytes,const struct fnv_uniform_plan *);
/* Idempotent only for identical bytecode, SPIR-V and uniform plan. Conflicts
 * reject without replacing live catalog storage; no inference or caps change. */
uint32_t fnv_vk_backend_ensure_shader(struct fnv_vk_backend *,enum fnv_d3d9_stage,const void *code,uint32_t code_bytes,const void *spirv,uint32_t spirv_bytes,const struct fnv_uniform_plan *);
/* Trusted original XYZ/UV fixed VS only; host validates semantic contract.
 * Retains an internal shader pin through Reset; id0 clears. Plan must be four
 * float column registers c0..3,64 bytes. No binding/caps changes. */
uint32_t fnv_vk_backend_fixed_vertex(struct fnv_vk_backend *,uintptr_t);
/* Trusted directional XYZ/NORMAL shader with ten contiguous float registers. */
/* Trusted zero-uniform PS: mode0 diffuse RGBA,1 texture*diffuse RGBA,
 * 2 texture*diffuse RGB with texture alpha,3 two RGBA MODULATE stages sharing
 * TEXCOORD0. Keeps public binding unchanged. */
uint32_t fnv_vk_backend_fixed_pixel(struct fnv_vk_backend *,uint32_t mode,uintptr_t);
uint32_t fnv_vk_backend_fixed_directional(struct fnv_vk_backend *,uintptr_t);
/* Supported logical D3D9 resource/usage -> actual Vulkan format query.
 * Unsupported capabilities return D3DERR_NOTAVAILABLE; never allocate here. */
uint32_t fnv_vk_backend_check_format(struct fnv_vk_backend *,uint32_t usage,uint32_t resource_type,uint32_t format);
/* Packed x86 D3DADAPTER_IDENTIFIER9 (1104 bytes), derived from the selected
 * Vulkan physical device. Own shim driver identity/version, no Windows driver
 * version encoding, PCI subsystem/revision or WHQL certificate is claimed.
 * DeviceIdentifier is the opaque Vulkan pipeline-cache UUID, not a serial ID.
 * Output untouched on invalid arguments; caller prevents concurrent teardown. */
uint32_t fnv_vk_backend_adapter_identifier(struct fnv_vk_backend *,void *,uint32_t bytes);
struct fnv_vk_vertex_input {uint32_t usage,index,location;};
/* Trusted offline pair contract: descriptor set 1/3 binding 0 are VS/PS UBOs,
 * set 2 contains combined pixel samplers at their D3D sampler binding numbers.
 * Copy semantic/location map and sampler mask; no inference from declaration order.
 * Caller verifies SPIR-V pair linkage and descriptors offline. */
uint32_t fnv_vk_backend_link_program(struct fnv_vk_backend *,const void *vertex,uint32_t vertex_bytes,const void *pixel,uint32_t pixel_bytes,const struct fnv_vk_vertex_input *,uint32_t inputs,uint32_t sampler_mask);
/* Reuse only an identical pair, semantic map and sampler mask. */
uint32_t fnv_vk_backend_ensure_program(struct fnv_vk_backend *,const void *vertex,uint32_t vertex_bytes,const void *pixel,uint32_t pixel_bytes,const struct fnv_vk_vertex_input *,uint32_t inputs,uint32_t sampler_mask);
/* Debug snapshots only, not GPU draws. Both output buffers are unchanged on
 * error; require disjoint valid destinations, exclusive caller ownership. */
uint32_t fnv_vk_backend_read_uniforms(struct fnv_vk_backend *,void *vertex,uint32_t vertex_capacity,void *pixel,uint32_t pixel_capacity);
uint32_t fnv_vk_backend_read_declaration(struct fnv_vk_backend *,uintptr_t,void *,uint32_t capacity);
/* Caller prevents concurrent teardown. BUSY if public references or submitted
 * work remain. Unsubmitted commands can be discarded only after device refs
 * reach zero. A permanently lost device is cleaned without claiming completion. */
uint32_t fnv_vk_backend_destroy(struct fnv_vk_backend *);
#endif
