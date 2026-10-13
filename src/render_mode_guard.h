#pragma once
#include <cstdint>

// The audited bootstrap call passes dimensions also stored in this global pair.
// Restrict this experiment to its original widescreen mode and aligned 16:9 sizes.
inline bool FnvCanOverrideRenderMode(uint32_t caller, uint32_t width, uint32_t height,
                                     uint32_t global_width, uint32_t global_height,
                                     uint32_t requested_width, uint32_t requested_height) {
    return caller == 0x8230F3F0 && width == 1280 && height == 720 &&
           global_width == width && global_height == height &&
           requested_width >= 640 && requested_width <= width &&
           requested_height >= 480 && requested_height <= height &&
           requested_width % 32 == 0 && requested_height % 4 == 0 &&
           uint64_t(requested_width) * 9 == uint64_t(requested_height) * 16;
}
