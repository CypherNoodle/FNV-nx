#ifndef FNV_D3D9_COM_H
#define FNV_D3D9_COM_H
#include "cpu_runtime.h"
#include "d3d9_constants.h"
#define FNV_D3D9_COM_METHOD_COUNT 97u
struct fnv_guest_memory;
struct fnv_d3d9_com;
enum fnv_d3d9_resource_kind { FNV_D3D9_VS,FNV_D3D9_PS,FNV_D3D9_VB,FNV_D3D9_IB,FNV_D3D9_DECL,FNV_D3D9_TEXTURE2D,FNV_D3D9_SURFACE,FNV_D3D9_STATEBLOCK };
enum fnv_d3d9_state_kind { FNV_D3D9_RENDER_STATE,FNV_D3D9_SAMPLER_STATE,FNV_D3D9_VIEWPORT,FNV_D3D9_SCISSOR,FNV_D3D9_TEXTURE_BINDING,FNV_D3D9_SHADER_BINDING };
struct fnv_d3d9_state_value {enum fnv_d3d9_state_kind kind;uint32_t sampler,name,values[6];};
struct fnv_d3d9_shader_code { const void *bytes;uint32_t size; };
/* Provider owns a real prepared device, initially with one transferred
 * reference, shader/stream/index/declaration/texture bindings null and stream
 * frequencies 1. Initial color/depth targets belong to the provider; getters
 * acquire their public references on demand, including implicit backbuffers.
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
    /* Standalone surfaces: flag is Lockable for color, Discard for depth.
     * get_target returns an OWNED public reference, including implicit surfaces.
     * Descriptors are eight packed D3DSURFACE_DESC DWORDs, no host pointers. */
    uint32_t (*create_surface)(void *,int depth,uint32_t width,uint32_t height,uint32_t format,uint32_t multisample,uint32_t quality,uint32_t flag,uintptr_t *);
    uint32_t (*surface_desc)(void *,uintptr_t,uint32_t values[8]);
    uint32_t (*set_target)(void *,int depth,uint32_t index,uintptr_t);
    uint32_t (*get_target)(void *,int depth,uint32_t index,uintptr_t *);
    /* SetRenderTarget must apply D3D9's implicit viewport reset too. Clear
     * receives bounded copied D3DRECT DWORDs; Z is raw float bits. Copy any
     * command data before return and retain resources through GPU completion. */
    uint32_t (*clear)(void *,uint32_t count,const uint32_t *rects,uint32_t flags,uint32_t color,uint32_t z_bits,uint32_t stencil);
    /* Device is initially outside a scene. Queue work without pretending
     * EndScene is a GPU fence. Dirty constants are uploaded before each draw;
     * a failed upload suppresses the draw and remains retryable. */
    uint32_t (*scene)(void *,int begin);
    fnv_d3d9_constants_upload upload_constants;
    /* BaseVertexIndex is transported as raw signed-32 bits. Backend validates
     * primitive/caps/ranges and owns all consumed state and resource versions. */
    uint32_t (*draw)(void *,int indexed,uint32_t primitive,uint32_t base_bits,uint32_t min_vertex,uint32_t vertices,uint32_t start,uint32_t primitives);
    /* Optional DISCARD/full-backbuffer presentation. Wait for queue space
     * without spinning. Timeout means device lost, never fake success or a
     * nonblocking BUSY result to the base D3D9 Present caller. */
    uint32_t (*present)(void *,uint64_t wait_timeout_ns);
    /* Fixed-configuration healthy-device Reset. Reject live DEFAULT resources;
     * preserve shader/managed objects, discard bindings/constants/state. No
     * success until old render work is complete and new targets are ready. */
    /* UP data must be copied before return; transient GPU versions retain fences.
     * After an attempt, clear stream zero, and indices for indexed UP. */
    uint32_t (*draw_up)(void *,int indexed,uint32_t primitive,uint32_t minimum,uint32_t vertices,uint32_t primitives,const void *indices,uint32_t format,const void *vertices_data,uint32_t stride,uint32_t vertex_bytes,uint32_t index_bytes);
    uint32_t (*reset)(void *,uint64_t wait_timeout_ns);
    uint32_t (*cooperative)(void *);
    /* State transactions, max 274 entries: 0 apply atomically,
     * 1 capture values atomically, 2 validate without changing state.
     * TEXTURE_BINDING uses sampler 0..15, name=0 and a host handle encoded
     * low/high DWORDs in values[0..1]. Private IDs, never guest pointers.
     * Capture does not acquire public resource references. SHADER_BINDING uses
     * sampler=VS/PS stage (0/1), same handle encoding, name=0. */
    uint32_t (*state_batch)(void *,struct fnv_d3d9_state_value *,uint32_t count,uint32_t operation);
    /* Internal retention for state-block resources, no public COM/device ref.
     * Called only with a live ID; pin counts are bounded by COM block capacity.
     * Releasing a pin is allowed during teardown after the device closes. */
    void (*resource_pin)(void *,uintptr_t,int add);
};
/* Optional single prepared virtual-adapter factory. Dimensions/formats must
 * match the provider's real implicit targets. No swapchain/caps are invented.
 * Query callback runs under COM lock; 0=CheckDeviceFormat, 1=GetDeviceCaps.
 * Output caps, when supported, are exactly 304 packed x86 D3DCAPS9 bytes. */
struct fnv_d3d9_factory_profile {
    uint32_t width,height,color_format,depth_format,refresh_hz;
    void *context;
    uint32_t (*query)(void *,uint32_t kind,const uint32_t *args,uint32_t count,void *out,uint32_t bytes);
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
    const struct fnv_d3d9_factory_profile *factory;
    /* Host watchdog per blocking present wait. Zero disables COM Present;
     * UINT64_MAX is invalid. The prepared native window must correspond to
     * presentation_window (guest HWND); factory CreateDevice replaces that
     * identity with hDeviceWindow, or hFocusWindow when hDeviceWindow is 0. */
    uint64_t presentation_timeout_ns;
    uint32_t presentation_window;
    uint64_t reset_timeout_ns; /* Zero disables Reset; finite per wait. */
};
/* One module for the single active flat-mapped CPU. Caller owns a dedicated
 * writable guest object range, disjoint from image/PEB/TEB/stack/data. Setup
 * precedes first attach. Optional factory publishes the single prepared device;
 * this is not a game launcher or a complete presentation implementation. */
enum fnv_cpu_result fnv_d3d9_com_create(struct fnv_cpu_runtime *,const struct fnv_d3d9_com_config *,struct fnv_d3d9_com **);
const struct fnv_cpu_export *fnv_d3d9_com_exports(const struct fnv_d3d9_com *,size_t *count);
uint32_t fnv_d3d9_com_device(const struct fnv_d3d9_com *);
/* Detach all threads first; unregisters methods and releases owned references.
 * Must precede CPU destroy. The caller prevents concurrent attach/teardown. */
enum fnv_cpu_result fnv_d3d9_com_destroy(struct fnv_d3d9_com *);
uint32_t fnv_d3d9_com_flush(struct fnv_d3d9_com *,fnv_d3d9_constants_upload,void *);
#endif
