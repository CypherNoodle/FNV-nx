#ifndef FNV_D3D9_GEOMETRY_H
#define FNV_D3D9_GEOMETRY_H
#include <stdint.h>
/* CPU-readable immutable buffer versions. Caller owns their lifetime and must
 * exclude concurrent updates. This validates accesses, not device caps, shader
 * linkage, format conversion or GPU synchronization. Packed declarations use
 * D3DVERTEXELEMENT9 bytes, including END. Instancing is not supported yet. */
struct fnv_geometry_stream { uint64_t bytes; uint32_t offset,stride,frequency; };
struct fnv_geometry_input {
    struct fnv_geometry_stream streams[16];
    const void *elements;
    uint32_t element_count;
    const void *indices;
    uint64_t index_bytes;
    uint32_t index_size,indexed,primitive,base_bits,min_vertex,vertices,start,primitives;
};
struct fnv_geometry_range { uint64_t offset,bytes; };
struct fnv_geometry_access {
    struct fnv_geometry_range streams[16],indices;
    uint32_t count,first_vertex,last_vertex,stream_mask;
};
/* Zero on success, D3DERR_INVALIDCALL otherwise. Output unchanged on failure.
 * Index data is little endian and may be unaligned. Range results bound all
 * actual accesses (including holes), not a promise of contiguous vertex use. */
uint32_t fnv_d3d9_geometry_validate(const struct fnv_geometry_input *,struct fnv_geometry_access *);
#endif
