#ifndef FNV_D3D9_VK_STATE_H
#define FNV_D3D9_VK_STATE_H
#include <stdint.h>
#include <vulkan/vulkan_core.h>
/* Raw D3D9 DWORD state. Initial subset: Z buffer (no W), compare/write,
 * point/linear filters and wrap/mirror/clamp/border. No anisotropy, sRGB view,
 * max-mip remapping, stencil, depth bounds or dynamic state management. */
struct fnv_d3d9_depth { uint32_t enable,write,compare; };
struct fnv_d3d9_sampler {
    uint32_t address_u,address_v,address_w,border,mag,min,mip,lod_bias_bits,max_mip,max_anisotropy;
};
/* Pure translation, no Vulkan calls or allocation. Output unchanged on error
 * (D3DERR_INVALIDCALL). Caller owns capability negotiation and snapshots.
 * Sampler caps provide the actual image mip count and device maxSamplerLodBias.
 * Unsupported values fail explicitly. D3D booleans normalize nonzero to true. */
uint32_t fnv_d3d9_vk_depth(const struct fnv_d3d9_depth *,VkPipelineDepthStencilStateCreateInfo *);
uint32_t fnv_d3d9_vk_sampler(const struct fnv_d3d9_sampler *,uint32_t levels,float max_lod_bias,VkSamplerCreateInfo *);
#endif
