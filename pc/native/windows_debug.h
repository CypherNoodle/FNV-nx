#ifndef FNV_WINDOWS_DEBUG_H
#define FNV_WINDOWS_DEBUG_H
#include "cpu_runtime.h"
/* Bounded debugger-compatible current-thread naming event only.
 * This is not guest SEH or a general exception dispatcher. */
const struct fnv_cpu_export *fnv_windows_debug_exports(size_t *);
#endif
