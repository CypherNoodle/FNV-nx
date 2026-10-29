#ifndef FNV_WINDOWS_VORBIS_H
#define FNV_WINDOWS_VORBIS_H
#include "windows_thread.h"
struct fnv_windows_vorbis;
/* Read-only callback streams. Each slot owns 0x11000 committed guest RW bytes
 * for callback scratch and eight stable Windows32 vorbis_info views. */
struct fnv_vorbis_config {uint32_t arena_address,arena_bytes,streams;struct fnv_guest_memory *memory;};
enum fnv_cpu_result fnv_windows_vorbis_create(struct fnv_windows_process *,const struct fnv_vorbis_config *,struct fnv_windows_vorbis **);
enum fnv_cpu_result fnv_windows_vorbis_destroy(struct fnv_windows_vorbis *);
const struct fnv_cpu_export *fnv_windows_vorbis_exports(size_t *);
struct fnv_vorbis_metrics {uint64_t opens,open_failures,reads,decoded_bytes;int32_t last_error;};
int fnv_windows_vorbis_metrics(struct fnv_vorbis_metrics *);
#endif
