#ifndef FNV_D3D9_COM_H
#define FNV_D3D9_COM_H
#include "cpu_runtime.h"
#include "d3d9_constants.h"
struct fnv_guest_memory;
struct fnv_d3d9_com;
struct fnv_d3d9_shader_code { const void *bytes;uint32_t size; };
/* Provider owns a real prepared device, initially with one transferred
 * reference. A shader returned successfully has one reference and retains
 * its parent device. Its final release drops that parent reference too.
 * Callbacks run under the module lock: no reentry, exceptions or longjmp.
 * No callback may report success without a valid backend shader object. */
struct fnv_d3d9_shader_backend {
    void *context;
    uint32_t (*create)(void *,enum fnv_d3d9_stage,const void *,uint32_t,uintptr_t *shader);
    void (*addref)(void *,uintptr_t shader);
    void (*release)(void *,uintptr_t shader);
    void (*device_addref)(void *);
    void (*device_release)(void *);
};
struct fnv_d3d9_com_config {
    uint32_t objects_address,objects_bytes,shader_capacity;
    uint32_t vertex_floats,pixel_floats;
    const struct fnv_d3d9_shader_code *catalog;
    size_t catalog_count;
    struct fnv_d3d9_shader_backend backend;
    struct fnv_guest_memory *memory; /* Optional permission-aware data pool. */
};
/* One module for the single active flat-mapped CPU. Caller owns a dedicated
 * writable guest object range, disjoint from image/PEB/TEB/stack/data. Setup
 * precedes first attach. Not Direct3DCreate9/CreateDevice or a game launcher. */
enum fnv_cpu_result fnv_d3d9_com_create(struct fnv_cpu_runtime *,const struct fnv_d3d9_com_config *,struct fnv_d3d9_com **);
uint32_t fnv_d3d9_com_device(const struct fnv_d3d9_com *);
/* Detach all threads first; unregisters methods and releases owned references.
 * Must precede CPU destroy. The caller prevents concurrent attach/teardown. */
enum fnv_cpu_result fnv_d3d9_com_destroy(struct fnv_d3d9_com *);
uint32_t fnv_d3d9_com_flush(struct fnv_d3d9_com *,fnv_d3d9_constants_upload,void *);
#endif
