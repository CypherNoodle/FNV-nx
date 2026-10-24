#ifndef FNV_WINDOWS_STRINGS_H
#define FNV_WINDOWS_STRINGS_H
#include "cpu_runtime.h"
/* Stateless guest byte-string helpers. Bounded scanning, no host DLL calls. */
const struct fnv_cpu_export *fnv_windows_strings_exports(size_t *);
#endif
