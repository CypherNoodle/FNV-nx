#ifndef FNV_WINDOWS_SYSTEM_H
#define FNV_WINDOWS_SYSTEM_H
#include "windows_thread.h"
struct fnv_windows_system;
/* Virtual i686 topology, independent of host ARM/x64 hardware. Set explicitly;
 * thread capacity is a resource budget, not a processor count. */
enum fnv_cpu_result fnv_windows_system_create(struct fnv_windows_process *,uint32_t logical_processors,struct fnv_windows_system **);
enum fnv_cpu_result fnv_windows_system_destroy(struct fnv_windows_system *);
const struct fnv_cpu_export *fnv_windows_system_exports(size_t *count);
/* Attached thread only, actual immutable flat guest-arena bounds. */
void fnv_windows_current_address_bounds(uint32_t *base,uint32_t *bytes);
#endif
