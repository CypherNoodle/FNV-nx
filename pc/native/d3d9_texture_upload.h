#ifndef FNV_D3D9_TEXTURE_UPLOAD_H
#define FNV_D3D9_TEXTURE_UPLOAD_H
#include <stddef.h>
#include <stdint.h>
/* CPU staging conversion for one uncompressed mip patch. Source points to the
 * patch's first pixel, not the full texture origin. Both buffers are linear,
 * little endian; pitch includes row padding. Caller provides valid immutable
 * source storage and excludes concurrent destination access. */
struct fnv_texture_patch {
    uint32_t width,height,left,top,copy_width,copy_height,format,source_pitch;
    const void *source;
    size_t source_bytes;
};
/* D3D formats 21 ARGB8,22 XRGB8,23 RGB565,28 A8,50 L8 -> RGBA8 UNORM.
 * RGB565 rounds each channel to nearest 8-bit UNORM; this is quantization,
 * not a claim of bit-exact native 565 filtering. No sRGB decode/premultiply.
 * Unsupported formats (including BC) fail before any destination write.
 * Destination describes a full mip; bytes outside the patch stay unchanged.
 * Rejects overlapping buffers and insufficient sizes; output unchanged on
 * any validation error. No allocation, locks, Vulkan calls or GPU ownership. */
uint32_t fnv_d3d9_texture_rgba8(const struct fnv_texture_patch *,void *destination,size_t destination_bytes,uint32_t destination_pitch);
#endif
