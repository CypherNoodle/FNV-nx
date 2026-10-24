#ifndef FNV_WINDOWS_USER32_INPUT_H
#define FNV_WINDOWS_USER32_INPUT_H
#include "windows_user32.h"
#include "dinput8.h"
/* Fixed US PC physical layout: DIK scan codes are NOT Windows VK codes.
 * Uses physical mouse button order; no toggles or text/IME translation.
 * Rejects unsupported held keys and malformed bytes without publishing. */
enum fnv_cpu_result fnv_user32_update_di_snapshot(struct fnv_windows_user32 *,const struct fnv_di_snapshot *,int focused);
#endif
