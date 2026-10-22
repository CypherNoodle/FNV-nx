#ifndef FNV_D3D9_SDP_H
#define FNV_D3D9_SDP_H
#include <stddef.h>
struct fnv_d3d9_shader_code;
struct fnv_d3d9_sdp;
enum fnv_sdp_result { FNV_SDP_OK, FNV_SDP_INVALID, FNV_SDP_LIMIT, FNV_SDP_MEMORY, FNV_SDP_IO };
/* Structural validation only, not bytecode translation or capability proof.
 * On failure *out is NULL. Budget includes owned input and descriptor storage.
 * Entries point into immutable owned storage until destroy. COM create copies
 * these entries; callers may destroy the package after successful COM create. */
enum fnv_sdp_result fnv_sdp_parse(const void *,size_t,size_t budget,struct fnv_d3d9_sdp **out);
enum fnv_sdp_result fnv_sdp_load(const char *path,size_t budget,struct fnv_d3d9_sdp **out);
const struct fnv_d3d9_shader_code *fnv_sdp_entries(const struct fnv_d3d9_sdp *,size_t *count);
void fnv_sdp_destroy(struct fnv_d3d9_sdp *);
#endif
