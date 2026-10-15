#ifndef FNV_WINDOWS_THREAD_H
#define FNV_WINDOWS_THREAD_H
#include "cpu_runtime.h"

#define FNV_WINDOWS_TLS_SLOTS 1088u
#define FNV_WINDOWS_TEB_BYTES 4096u
#define FNV_WINDOWS_TLS_EXPANSION_BYTES 4096u
#define FNV_WINDOWS_THREAD_EXPORTS 8u
struct fnv_windows_process;
struct fnv_windows_config {
    struct fnv_cpu_config cpu;
    uint32_t image_bytes, process_id, peb_address;
};
struct fnv_windows_thread_config {
    struct fnv_cpu_state state;
    uint32_t thread_id, teb_address, tls_expansion_address;
    uint32_t stack_low, stack_high, static_tls_vector, static_tls_vector_bytes;
};
/* Owns its CPU runtime. Caller owns the arena and reserves separate image,
 * PEB (one page), TEB, expansion and stack regions. The PEB and optional
 * static TLS vector must already be prepared by the caller; no PE TLS
 * template/callback loading, Windows SEH, VirtualAlloc or CreateThread here. */
enum fnv_cpu_result fnv_windows_create(const struct fnv_windows_config *, struct fnv_windows_process **);
enum fnv_cpu_result fnv_windows_destroy(struct fnv_windows_process *);
enum fnv_native_result fnv_windows_bind_imports(struct fnv_windows_process *,
    struct fnv_native_image *, struct fnv_native_import *, size_t capacity,
    const struct fnv_cpu_export *additional, size_t additional_count, size_t *count);
/* Use these lifecycle functions together; do not bypass them with cpu_attach/
 * detach. No thread may use a destroyed process or alter its reserved TEB
 * pointers. Get/Set TLS serialize with index recycling, not guest execution. */
enum fnv_cpu_result fnv_windows_thread_attach(struct fnv_windows_process *, const struct fnv_windows_thread_config *);
enum fnv_cpu_result fnv_windows_thread_detach(void);
#endif
