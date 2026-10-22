#ifndef FNV_WINDOWS_COM_H
#define FNV_WINDOWS_COM_H
#include "cpu_runtime.h"
/* Initialization exports only. Object providers must implement their own ABI. */
const struct fnv_cpu_export *fnv_windows_com_exports(size_t *count);
#endif
