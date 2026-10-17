#ifndef FNV_D3D9_CONSTANTS_H
#define FNV_D3D9_CONSTANTS_H
#include <stdint.h>
#define FNV_D3D9_OK 0u
#define FNV_D3D9_INVALIDCALL 0x8876086Cu
#define FNV_D3D9_OUTOFMEMORY 0x8007000Eu
#define FNV_D3D9_UPLOAD_FAILED 0x80004005u
enum fnv_d3d9_stage { FNV_D3D9_VERTEX,FNV_D3D9_PIXEL };
enum fnv_d3d9_constant_kind { FNV_D3D9_FLOAT,FNV_D3D9_INTEGER,FNV_D3D9_BOOLEAN };
struct fnv_d3d9_constants;
/* Selection masks and owned register bits for recorded state blocks. A zeroed
 * snapshot selects nothing. BOOL values retain their opaque 32-bit contents. */
struct fnv_d3d9_constant_snapshot {
    uint64_t mask[2][3][4];uint32_t floats[2][256][4],integers[2][16][4],booleans[2][16];
};
uint32_t fnv_d3d9_constants_record(struct fnv_d3d9_constants *,struct fnv_d3d9_constant_snapshot *,enum fnv_d3d9_stage,enum fnv_d3d9_constant_kind,uint32_t first,uint32_t count,const void *bits);
/* 0 apply selected values and mark changes dirty, 1 capture selected values,
 * 2 validate masks only. Invalid masks leave banks and snapshot untouched. */
uint32_t fnv_d3d9_constants_snapshot(struct fnv_d3d9_constants *,struct fnv_d3d9_constant_snapshot *,uint32_t operation);
/* Global API register banks, not shader-local DEF/DEFI/DEFB constants.
 * Float/INT vectors are opaque 4x32-bit values; BOOL registers are 32 bits.
 * Limits must agree with the eventual advertised device caps/profile. */
uint32_t fnv_d3d9_constants_create(uint32_t vertex_floats,uint32_t pixel_floats,struct fnv_d3d9_constants **);
void fnv_d3d9_constants_destroy(struct fnv_d3d9_constants *);
uint32_t fnv_d3d9_constants_set(struct fnv_d3d9_constants *,enum fnv_d3d9_stage,enum fnv_d3d9_constant_kind,uint32_t first,uint32_t count,const void *bits);
uint32_t fnv_d3d9_constants_get(struct fnv_d3d9_constants *,enum fnv_d3d9_stage,enum fnv_d3d9_constant_kind,uint32_t first,uint32_t count,void *bits);
/* Upload callback copies data to owned storage before returning nonzero.
 * Called under a blocking lock: no reentry, exceptions or longjmp. A failed
 * run remains dirty, as do subsequent runs. Earlier accepted runs stay clean.
 * Do not draw after a failed flush. This is not a GPU lifetime/fence manager. */
typedef int (*fnv_d3d9_constants_upload)(void *,enum fnv_d3d9_stage,enum fnv_d3d9_constant_kind,uint32_t first,uint32_t count,const uint32_t *bits);
uint32_t fnv_d3d9_constants_flush(struct fnv_d3d9_constants *,fnv_d3d9_constants_upload,void *);
/* Required when the backend replaces the bank storage or its layout. */
void fnv_d3d9_constants_invalidate(struct fnv_d3d9_constants *);
#endif
