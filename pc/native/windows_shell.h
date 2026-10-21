#ifndef FNV_WINDOWS_SHELL_H
#define FNV_WINDOWS_SHELL_H
#include "cpu_runtime.h"
/* Small, explicitly virtualized Shell32 folder subset. */
const struct fnv_cpu_export *fnv_windows_shell_exports(size_t *count);
#endif

