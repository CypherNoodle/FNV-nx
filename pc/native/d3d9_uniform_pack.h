#ifndef FNV_D3D9_UNIFORM_PACK_H
#define FNV_D3D9_UNIFORM_PACK_H
#include <stdint.h>
/* Immutable, reflected shader-specific plan. kind: 0 float4, 1 int4, 2 BOOL.
 * Shader-local DEF/DEFI/DEFB never belong in this global-API upload plan.
 * Entries describe 16-byte slots, including std140 BOOL padding.
 */
struct fnv_uniform_entry { uint32_t kind, first, count, offset, stride; };
struct fnv_uniform_plan {
    uint32_t bytes, count;
    const struct fnv_uniform_entry *entries;
};
struct fnv_uniform_banks {
    const uint32_t *bits[3];
    uint32_t count[3];
};
/* No allocation, locking, GPU submission or lifetime management. Caller holds
 * an immutable bank snapshot. All validation precedes output modification.
 * Float/INT bit patterns are copied; BOOL becomes 0/1 and padding is zero.
 * Returns 0 on success, 0x8876086C on invalid plan/range/overlap/capacity.
 */
uint32_t fnv_d3d9_uniform_pack(const struct fnv_uniform_plan *,
    const struct fnv_uniform_banks *, void *destination, uint32_t capacity);
#endif
