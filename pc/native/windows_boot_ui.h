#ifndef FNV_WINDOWS_BOOT_UI_H
#define FNV_WINDOWS_BOOT_UI_H
#include "cpu_runtime.h"
struct fnv_windows_process;
struct fnv_windows_boot_ui;
struct fnv_windows_user32;
struct fnv_boot_window_request {uint32_t width,height,style;int32_t x,y;const char *title;};
/* Own a native presentation object from create until destroy. No guest calls,
 * no longjmp, no provider locks in these callbacks. create returns 0 on failure. */
struct fnv_boot_window_backend {
    void *context;
    int (*create)(void *,const struct fnv_boot_window_request *,void **);
    void (*destroy)(void *,void *);
};
/* Configure before attach. desktop must outlive this provider. */
enum fnv_cpu_result fnv_windows_boot_ui_set_backend(struct fnv_windows_boot_ui *,struct fnv_windows_user32 *,const struct fnv_boot_window_backend *);
/* Borrowed native presentation object; never a host pointer in guest memory.
 * Owner thread only during window lifetime; release renderer before destroy. */
enum fnv_cpu_result fnv_windows_boot_ui_presentation(struct fnv_windows_boot_ui *,uint32_t hwnd,void **);
struct fnv_boot_class {uint32_t atom,style,procedure,instance,icon,brush;char name[256];};
/* Bounded borderless top-level window lifecycle. CreateWindowEx requires
 * configured native presentation backing and invokes the guest procedure. */
enum fnv_cpu_result fnv_windows_boot_ui_create(struct fnv_windows_process *,const struct fnv_native_image *,struct fnv_windows_boot_ui **);
enum fnv_cpu_result fnv_windows_boot_ui_destroy(struct fnv_windows_boot_ui *);
const struct fnv_cpu_export *fnv_windows_boot_ui_exports(size_t *);
enum fnv_cpu_result fnv_windows_boot_ui_class(struct fnv_windows_boot_ui *,uint32_t atom,struct fnv_boot_class *);
#endif
