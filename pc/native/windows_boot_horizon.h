#ifndef FNV_WINDOWS_BOOT_HORIZON_H
#define FNV_WINDOWS_BOOT_HORIZON_H
#include "windows_boot_ui.h"
#include <stdatomic.h>
/* Single lease of a caller-owned initialized libnx NWindow. The renderer and
 * WSI must be released before this binding/provider is destroyed. Guest client
 * dimensions are metadata; Vulkan config must match actual native dimensions.
 * Does not close or resize the caller's/default window. */
struct fnv_boot_horizon {void *nwindow;atomic_int leased;};
int fnv_boot_horizon_backend(struct fnv_boot_horizon *,void *nwindow,struct fnv_boot_window_backend *);
#endif
