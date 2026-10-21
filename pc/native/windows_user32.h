#ifndef FNV_WINDOWS_USER32_H
#define FNV_WINDOWS_USER32_H
#include "cpu_runtime.h"
/* Native local-console USER32 subset. No host HWND or remote session. */
const struct fnv_cpu_export *fnv_windows_user32_exports(size_t *count);
struct fnv_windows_process;
struct fnv_windows_user32;
/* Native renderer/window adapters register only virtual top-level windows.
 * Fixed capacity16; handles are guest tokens and are never host HWND values.
 * A single desktop is allowed, created before guest attach and destroyed after
 * detach. Runtime registration enforces the latter lifecycle at destroy. */
enum fnv_cpu_result fnv_windows_user32_create(struct fnv_windows_process *,struct fnv_windows_user32 **);
enum fnv_cpu_result fnv_windows_user32_destroy(struct fnv_windows_user32 *);
enum fnv_cpu_result fnv_windows_user32_add_window(struct fnv_windows_user32 *,const char *class_name,const char *title,uint32_t *handle);
enum fnv_cpu_result fnv_windows_user32_remove_window(struct fnv_windows_user32 *,uint32_t handle);
#endif
