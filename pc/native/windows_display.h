#ifndef FNV_WINDOWS_DISPLAY_H
#define FNV_WINDOWS_DISPLAY_H
#include "windows_thread.h"
struct fnv_windows_display;
/* Fixed-capacity ANSI byte strings, each NUL-terminated. No host pointers in
 * the serialized guest DISPLAY_DEVICEA. Adapter/monitor selection and identity
 * come from the prepared backend, never an assumed PC vendor. */
struct fnv_display_device {
    char name[32],description[128];uint32_t state_flags;char id[128],key[128];
};
struct fnv_display_backend {
    void *context;
    /* Native only, thread-safe, no guest callbacks. parent=NULL: adapters;
     * parent=device name: monitors. 1=found,0=end,-1=unavailable/error.
     * flags0/1; identity strings remain backend-owned facts. */
    int (*enumerate)(void *,const char *parent,uint32_t index,uint32_t flags,struct fnv_display_device *);
};
/* Setup before attach, destroy after all detach. Backend/context retained by
 * caller. A missing backend is an explicit diagnostic, not an empty list. */
enum fnv_cpu_result fnv_windows_display_create(struct fnv_windows_process *,const struct fnv_display_backend *,struct fnv_windows_display **);
enum fnv_cpu_result fnv_windows_display_destroy(struct fnv_windows_display *);
const struct fnv_cpu_export *fnv_windows_display_exports(size_t *);
#endif
