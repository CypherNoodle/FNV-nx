#ifndef FNV_WINDOWS_FILETIME_H
#define FNV_WINDOWS_FILETIME_H
#include "cpu_runtime.h"
/* Stateless FILETIME comparison; no timezone or wall-clock sampling. */
const struct fnv_cpu_export *fnv_windows_filetime_exports(size_t *);
#endif
