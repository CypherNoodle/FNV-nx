#ifndef FNV_SWITCH_ENGINE_LAYOUT_H
#define FNV_SWITCH_ENGINE_LAYOUT_H
#include <stdint.h>

/* Captured-PC engine experiment. Not the bounded CRT diagnostic profile.
 * Offsets are relative to the PE image base, not host pointers. */
enum fnv_engine_region_id {
    FNV_ENGINE_IMAGE, FNV_ENGINE_PEB, FNV_ENGINE_PARAMETERS,
    FNV_ENGINE_MAIN_THREAD, FNV_ENGINE_HEAP, FNV_ENGINE_WORKERS,
    FNV_ENGINE_VM, FNV_ENGINE_REGION_COUNT
};
struct fnv_engine_region { uint32_t offset, bytes; };
struct fnv_engine_layout {
    uint32_t base, arena_bytes, commit_limit;
    uint32_t worker_capacity, worker_stack_bytes, worker_tls_bytes;
    uint32_t worker_host_stack_bytes;
    struct fnv_engine_region regions[FNV_ENGINE_REGION_COUNT];
};
/* Transactional: invalid image/base leaves output unchanged. */
int fnv_engine_layout_create(uint32_t base,uint32_t image_bytes,struct fnv_engine_layout *);
/* Checks all address arithmetic, disjoint regions and worker backing. */
int fnv_engine_layout_valid(const struct fnv_engine_layout *);
#endif
