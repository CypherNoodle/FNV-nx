#ifndef FNV_WINDOWS_REGISTRY_H
#define FNV_WINDOWS_REGISTRY_H
#include "cpu_runtime.h"
/* Portable empty registry: predefined roots exist, no subkeys are installed.
 * Never opens the host registry. Nonempty supported paths return NOT_FOUND.
 * No values, writes, links, remote registry or allocated key handles. */
const struct fnv_cpu_export *fnv_windows_registry_exports(size_t *);
#endif
