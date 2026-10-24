#ifndef FNV_WINDOWS_MMIO_H
#define FNV_WINDOWS_MMIO_H
#include "windows_thread.h"
struct fnv_windows_mmio;
/* Eight read-only custom guest I/O streams. Caller owns >=0x11000 committed
 * guest RW bytes, disjoint from other providers, through destroy. No native
 * file paths or codecs are substituted for the guest I/O procedure. */
struct fnv_mmio_config {uint32_t arena_address,arena_bytes;struct fnv_guest_memory *memory;};
enum fnv_cpu_result fnv_windows_mmio_create(struct fnv_windows_process *,const struct fnv_mmio_config *,struct fnv_windows_mmio **);
enum fnv_cpu_result fnv_windows_mmio_destroy(struct fnv_windows_mmio *);
const struct fnv_cpu_export *fnv_windows_mmio_exports(size_t *);
#endif
