#ifndef FNV_CPU_NATIVE_OBSERVER_H
#define FNV_CPU_NATIVE_OBSERVER_H
#include <stdint.h>
/* Diagnostic only. Install before attach/reset after every thread detaches.
 * Called on the owning guest thread before native dispatch, outside provider
 * locks. Must return normally, preserve CPU state and never invoke guest code.
 * NULL disables it. No sampling of direct calls or pure guest loops. */
typedef void (*fnv_cpu_native_observer)(uint32_t entry);
void fnv_cpu_set_native_observer(fnv_cpu_native_observer);
#endif
