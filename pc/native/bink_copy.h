#ifndef FNV_BINK_COPY_H
#define FNV_BINK_COPY_H
#include "bink_decoder.h"
enum fnv_bink_copy_result {FNV_BINK_COPY_OK,FNV_BINK_COPY_INVALID,FNV_BINK_COPY_UNSUPPORTED};
/* Copy decoded RGBA to Bink surface32(BGRX) or32R(RGBX), X=0. Caller supplies
 * actual accessible capacities, distinct host buffers; not guest pointers.
 * Source bytes remain live/immutable through this synchronous copy. Positive
 * pitch only; scaling/alpha formats unsupported. Invalid input never writes.
 * Flags permit COPYALL and explicit NOSCALING (or default no scaling).
 * Does not upload textures, pace playback or implement the guest stdcall ABI. */
enum fnv_bink_copy_result fnv_bink_copy_rect(const struct fnv_bink_frame *,size_t rgba_bytes,
    void *destination,size_t destination_bytes,int32_t pitch,uint32_t destination_height,
    uint32_t dx,uint32_t dy,uint32_t sx,uint32_t sy,uint32_t width,uint32_t height,uint32_t flags);
#endif
