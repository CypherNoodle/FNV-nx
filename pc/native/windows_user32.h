#ifndef FNV_WINDOWS_USER32_H
#define FNV_WINDOWS_USER32_H
#include "cpu_runtime.h"
/* Native local-console USER32 subset. No host HWND or remote session. */
const struct fnv_cpu_export *fnv_windows_user32_exports(size_t *count);
#endif
