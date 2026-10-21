#ifndef FNV_WINDOWS_PROFILES_H
#define FNV_WINDOWS_PROFILES_H
#include "cpu_runtime.h"
/* Read-only sandboxed game INI subset; no Windows registry/directory lookup. */
const struct fnv_cpu_export *fnv_windows_profile_exports(size_t *count);
#endif
