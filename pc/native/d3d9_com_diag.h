#ifndef FNV_D3D9_COM_DIAG_H
#define FNV_D3D9_COM_DIAG_H
/* Probe-only observation API for the D3D9 module.
 *
 * Deliberately separate from d3d9_com.h: the shared CPU and CFG-correction
 * proofs pinned the public COM contract headers, and diagnostics must not
 * invalidate those proofs. These entry points only publish measurements or
 * route trace text to a host sink; they never alter an API result and never
 * report success to the guest. */
#include <stddef.h>
#include <stdint.h>
struct fnv_d3d9_com;
struct fnv_vk_backend;
/* Read-only snapshot, safe without the COM lock while the guest thread is
 * outside a COM call. Not a conformance or lifetime contract. */
struct fnv_d3d9_com_stats {
    uint32_t factory_refs,factory_created,device_references;
    uint32_t create_attempts,create_successes,create_failures,last_create_result;
};
int fnv_d3d9_com_stats(const struct fnv_d3d9_com *,struct fnv_d3d9_com_stats *);
/* Factory object address, or zero when no prepared factory is published. */
uint32_t fnv_d3d9_com_factory(const struct fnv_d3d9_com *);
/* Diagnostic trace sink. NULL disables the trace. The runtime formats one
 * bounded line per call; the sink must not reenter the COM module. Set before
 * the first guest attach. The buffer is private to the call. */
extern void (*fnv_d3d9_trace_hook)(const char *line);
void fnv_d3d9_trace(const char *format,...);
/* Host transfer provider for the implicit lockable backbuffer.
 *
 * D3DPRESENTFLAG_LOCKABLE_BACKBUFFER is a real capability, so the COM only
 * accepts it after a provider that can copy the complete colour target both
 * ways has been installed. The callbacks receive the provider surface handle
 * and a tightly packed BGRA8 host buffer of exactly width*height*4 bytes.
 * They must drain submitted work, wait a real fence and fail honestly on
 * timeout/loss. NULL callbacks leave the flag unsupported; no lock ever
 * reports success without a completed transfer. Set before the guest attach. */
struct fnv_d3d9_surface_io {
    void *context;
    uint32_t (*read)(void *,uintptr_t,void *,size_t,uint64_t);
    uint32_t (*write)(void *,uintptr_t,const void *,size_t,uint64_t);
};
void fnv_d3d9_com_surface_io(struct fnv_d3d9_com *,const struct fnv_d3d9_surface_io *);
/* Probe-only backend half of that contract, declared here because the pinned
 * public backend header must not change. Same validation and fence rules as
 * fnv_vk_backend_read_color; rejects depth/shadow-alias surfaces. */
uint32_t fnv_vk_backend_write_color(struct fnv_vk_backend *,uintptr_t,const void *,size_t,uint64_t);
#endif
