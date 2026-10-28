#ifndef FNV_WINDOWS_DIAGNOSTICS_H
#define FNV_WINDOWS_DIAGNOSTICS_H
#include "windows_thread.h"
/* Each provider is sampled independently. A bit in available means that part
 * was copied under its lock; unavailable parts are zero, never stale data. */
enum {FNV_DIAG_FILES=1,FNV_DIAG_HEAPS=2,FNV_DIAG_HANDLES=4,FNV_DIAG_SYNC=8,FNV_DIAG_WORKERS=16};
struct fnv_windows_diagnostics {
    unsigned available;
    struct fnv_windows_file_stats files;
    struct fnv_windows_heap_stats heaps;
    struct fnv_windows_handle_stats handles;
    struct fnv_windows_sync_stats sync;
    struct fnv_windows_worker_stats workers;
};
enum fnv_cpu_result fnv_windows_try_diagnostics(struct fnv_windows_process *,struct fnv_windows_diagnostics *);
/* Owner-thread name copy; zero on contention or no attached guest. */
int fnv_windows_try_debug_name(char out[128]);
#endif
