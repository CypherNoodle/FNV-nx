#ifndef FNV_STEAM_CALLBACKS_H
#define FNV_STEAM_CALLBACKS_H
#include "cpu_runtime.h"
struct fnv_windows_process;
struct fnv_steam_callbacks;
/* Registration only: no Steam client, authentication, interface or delivery.
 * One process; create before attach, destroy after every thread detaches.
 * Guest objects must outlive registration. Capacity is 128 unique objects. */
enum fnv_cpu_result fnv_steam_callbacks_create(struct fnv_windows_process *,struct fnv_steam_callbacks **);
enum fnv_cpu_result fnv_steam_callbacks_destroy(struct fnv_steam_callbacks *);
const struct fnv_cpu_export *fnv_steam_callbacks_exports(size_t *);
enum fnv_cpu_result fnv_steam_callbacks_count(struct fnv_steam_callbacks *,uint32_t *);
#endif
