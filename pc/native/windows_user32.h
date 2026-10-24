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
/* Optional immutable native display metrics; native callbacks only, 1=value,
 * 0=unsupported,-1=backend failure. Host/context live through destroy.
 * SM_SWAPBUTTON/REMOTESESSION retain their guest-local semantics. */
struct fnv_user32_metrics {void *context;int (*query)(void *,uint32_t,int32_t *);};
enum fnv_cpu_result fnv_windows_user32_create_with_metrics(struct fnv_windows_process *,const struct fnv_user32_metrics *,struct fnv_windows_user32 **);
enum fnv_cpu_result fnv_windows_user32_create(struct fnv_windows_process *,struct fnv_windows_user32 **);
enum fnv_cpu_result fnv_windows_user32_destroy(struct fnv_windows_user32 *);
enum fnv_cpu_result fnv_windows_user32_add_window(struct fnv_windows_user32 *,const char *class_name,const char *title,uint32_t *handle);
enum fnv_cpu_result fnv_windows_user32_remove_window(struct fnv_windows_user32 *,uint32_t handle);
/* Bounded posted thread-message queue, independent of host USER32. No pointer
 * payload lifetime is inferred. Producer retains desktop through this call.
 * Window-targeted, sent, paint and timer messages are not implemented here. */
struct fnv_user32_thread_message {uint32_t message,wparam,lparam,time;int32_t x,y;};
enum fnv_cpu_result fnv_windows_user32_post_thread(struct fnv_windows_user32 *,uint32_t thread_id,const struct fnv_user32_thread_message *);
/* Explicit native input sample, indexed by Windows VK, each byte 0 or 128.
 * Side-specific shift/control/alt supply the aggregate VK entries. Inactive
 * input clears held and recent-press bits; no OS keyboard is queried here. */
enum fnv_cpu_result fnv_windows_user32_update_keys(struct fnv_windows_user32 *,const unsigned char keys[256],int focused);
#endif
