#ifndef FNV_WINDOWS_THREAD_H
#define FNV_WINDOWS_THREAD_H
#include "cpu_runtime.h"

#define FNV_WINDOWS_TLS_SLOTS 1088u
#define FNV_WINDOWS_TEB_BYTES 4096u
#define FNV_WINDOWS_TLS_EXPANSION_BYTES 4096u
#define FNV_WINDOWS_THREAD_EXPORTS 50u
struct fnv_windows_process;
struct fnv_guest_memory;
struct fnv_windows_config {
    struct fnv_cpu_config cpu;
    uint32_t image_bytes, process_id, peb_address;
    struct fnv_guest_memory *memory; /* Optional, disjoint managed data pool. */
    /* Optional dedicated RW heap arena, disjoint from VM/image/PEB/threads.
     * Caller owns committed backing within cpu.memory until process destroy.
     * Fixed budget; no host pointers are returned to guest code. */
    uint32_t heap_address,heap_bytes,heap_capacity;
    /* Private heaps reserve from memory (Virtual* pool). Zero disables them.
     * Ceiling per growable heap and number of live private heaps. */
    uint32_t private_heap_reserve,private_heap_limit;
    uint32_t critical_section_capacity; /* Optional preallocated host table. */
    uint32_t kernel_handle_capacity; /* Optional synchronization/thread handles, max 65536. */
    /* Optional dedicated, committed RW arena: each worker owns two TEB/TLS
     * pages plus worker_stack_bytes. Disjoint from all other guest arenas.
     * Fixed budget, no demand stack growth/guard pages. Host stack is separate.
     * IDs >= 0x80000000 are reserved for managed workers while enabled. */
    uint32_t worker_address,worker_bytes,worker_stack_bytes,worker_capacity;
    uint32_t worker_host_stack_bytes;
    uint32_t worker_tls_bytes; /* Optional page-aligned vector+static TLS budget per worker. */
};
struct fnv_windows_thread_config {
    struct fnv_cpu_state state;
    uint32_t thread_id, teb_address, tls_expansion_address;
    uint32_t stack_low, stack_high, static_tls_vector, static_tls_vector_bytes;
    uint32_t static_tls_data,static_tls_data_bytes;
};
/* Owns its CPU runtime. Caller owns the arena and reserves separate image,
 * PEB (one page), TEB, expansion and stack regions. The PEB and optional
 * static TLS vector may be supplied by the caller. After binding a callbackless
 * EXE TLS template, attach initializes its vector[0] and separate TLS data.
 * PE TLS callbacks, multiple TLS modules and Windows SEH remain unsupported. */
enum fnv_cpu_result fnv_windows_create(const struct fnv_windows_config *, struct fnv_windows_process **);
enum fnv_cpu_result fnv_windows_destroy(struct fnv_windows_process *);
/* Borrowed host view for preparing native modules before attach. The process
 * owns it; keep using Windows attach/detach, and destroy modules first. */
struct fnv_cpu_runtime *fnv_windows_runtime(struct fnv_windows_process *);
struct fnv_windows_sync_stats {
    uint32_t live_sections,owned_sections,waiting_threads;
    uint64_t blocking_waits,wakeups,try_failures;
};
/* Diagnostic snapshot; caller retains the process during this call. */
enum fnv_cpu_result fnv_windows_sync_stats(struct fnv_windows_process *,struct fnv_windows_sync_stats *);
struct fnv_windows_heap_stats {
    uint32_t live_heaps,live_blocks;
    uint64_t metadata_bytes,lookup_steps,allocation_steps,gap_steps;
};
enum fnv_cpu_result fnv_windows_heap_stats(struct fnv_windows_process *,struct fnv_windows_heap_stats *);
struct fnv_windows_handle_stats {
    uint32_t live_handles,waiting_threads;
    uint64_t blocking_waits,timeouts,sets,resets;
};
enum fnv_cpu_result fnv_windows_handle_stats(struct fnv_windows_process *,struct fnv_windows_handle_stats *);
struct fnv_windows_worker_stats {
    uint32_t active,finished,capacity;
    uint64_t created,reaped,failures;
};
enum fnv_cpu_result fnv_windows_worker_stats(struct fnv_windows_process *,struct fnv_windows_worker_stats *);
enum fnv_native_result fnv_windows_bind_imports(struct fnv_windows_process *,
    struct fnv_native_image *, struct fnv_native_import *, size_t capacity,
    const struct fnv_cpu_export *additional, size_t additional_count, size_t *count);
/* Use these lifecycle functions together; do not bypass them with cpu_attach/
 * detach. No thread may use a destroyed process or alter its reserved TEB
 * pointers. Get/Set TLS serialize with index recycling, not guest execution. */
enum fnv_cpu_result fnv_windows_thread_attach(struct fnv_windows_process *, const struct fnv_windows_thread_config *);
enum fnv_cpu_result fnv_windows_thread_detach(void);
/* For native import modules; require the matching attached Windows thread. */
struct fnv_guest_memory *fnv_windows_current_memory(void);
void fnv_windows_set_last_error(uint32_t value);
#endif
