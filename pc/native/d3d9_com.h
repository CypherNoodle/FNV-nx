#ifndef FNV_D3D9_COM_H
#define FNV_D3D9_COM_H
#include "cpu_runtime.h"
#include "d3d9_constants.h"
struct fnv_guest_memory;
struct fnv_d3d9_com;
enum fnv_d3d9_resource_kind { FNV_D3D9_VS,FNV_D3D9_PS,FNV_D3D9_VB,FNV_D3D9_IB,FNV_D3D9_DECL,FNV_D3D9_TEXTURE2D };
enum fnv_d3d9_state_kind { FNV_D3D9_RENDER_STATE,FNV_D3D9_SAMPLER_STATE,FNV_D3D9_VIEWPORT,FNV_D3D9_SCISSOR };
struct fnv_d3d9_shader_code { const void *bytes;uint32_t size; };
/* Provider owns a real prepared device, initially with one transferred
 * reference, all supported bindings null and stream frequencies 1.
 * A resource returned successfully has one public reference and retains
 * its parent device. Its final public release drops that parent reference.
 * Callbacks run under the module lock: no reentry, exceptions or longjmp.
 * No create callback may report success without a valid backend object.
 * Failed setters must leave the previous backend state intact. */
struct fnv_d3d9_shader_backend {
    void *context;
    uint32_t (*create)(void *,enum fnv_d3d9_stage,const void *,uint32_t,uintptr_t *shader);
    void (*addref)(void *,uintptr_t shader);
    void (*release)(void *,uintptr_t shader);
    void (*device_addref)(void *);
    void (*device_release)(void *);
    /* Optional extensions: missing providers stop with a pending diagnostic.
     * Bindings retain internal refs, not public COM refs or a device parent.
     * AddRef from public zero resurrects a bound object and retains its parent.
     * Final device_release drops all internal bindings. This is D3D9 lifetime. */
    uint32_t (*bind_shader)(void *,enum fnv_d3d9_stage,uintptr_t);
    uint32_t (*create_buffer)(void *,enum fnv_d3d9_resource_kind,uint32_t bytes,uint32_t usage,uint32_t fvf_or_format,uint32_t pool,uintptr_t *);
    /* Copy into owned/versioned backend storage before success. Failure must
     * preserve prior backend contents; Unlock remains pending for retry. */
    uint32_t (*update_buffer)(void *,uintptr_t,uint32_t offset,uint32_t bytes,uint32_t flags,const void *data);
    uint32_t (*bind_stream)(void *,uint32_t stream,uintptr_t,uint32_t offset,uint32_t stride);
    uint32_t (*bind_indices)(void *,uintptr_t);
    uint32_t (*stream_frequency)(void *,uint32_t stream,uint32_t setting);
    /* Packed D3DVERTEXELEMENT9 bytes, eight per element, including END.
     * Provider validates types/caps and copies the declaration before return. */
    uint32_t (*create_declaration)(void *,const void *elements,uint32_t count,uintptr_t *);
    uint32_t (*bind_declaration)(void *,uintptr_t);
    uint32_t (*create_texture)(void *,uint32_t width,uint32_t height,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *);
    /* Rows are pixels or compressed block rows. Copy before returning success;
     * failure preserves backend contents and leaves UnlockRect retryable.
     * DISCARD on level zero invalidates the entire texture version, all mips.
     * GPU storage/version lifetime must be protected by real completion fences. */
    uint32_t (*upload_texture)(void *,uintptr_t,uint32_t level,uint32_t left,uint32_t top,uint32_t width,uint32_t height,uint32_t flags,uint32_t pitch,const void *);
    uint32_t (*bind_texture)(void *,uint32_t sampler,uintptr_t);
    /* Provider owns authoritative state including D3D9 defaults. Values are
     * raw DWORDs: one for render/sampler, six for viewport, four for scissor.
     * Float/signed fields retain exact bits. Copy before return; no pointer
     * retention. Failed setters preserve state; failed getters write no guest
     * data. Unsupported state must return an error, never claim fake success. */
    uint32_t (*state)(void *,enum fnv_d3d9_state_kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get);
};
struct fnv_d3d9_com_config {
    uint32_t objects_address,objects_bytes,resource_capacity;
    uint32_t vertex_floats,pixel_floats;
    const struct fnv_d3d9_shader_code *catalog;
    size_t catalog_count;
    struct fnv_d3d9_shader_backend backend;
    struct fnv_guest_memory *memory; /* Optional permission-aware data pool. */
    /* Dedicated precommitted shadow arena, retained by the caller until
     * teardown. Separate from object/stack/image ranges. Zero disables buffers.
     * No kernel allocations or page reprotection per buffer or per lock. */
    uint32_t buffer_address,buffer_bytes;
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
