#ifndef FNV_WINDOWS_BOOT_UI_H
#define FNV_WINDOWS_BOOT_UI_H
#include "cpu_runtime.h"
struct fnv_windows_process;
struct fnv_windows_boot_ui;
struct fnv_boot_class {uint32_t atom,style,procedure,instance,icon,brush;char name[256];};
/* Class/resource metadata only. Does not create a host window or substitute
 * a successful CreateWindowEx without message delivery/presentation backing. */
enum fnv_cpu_result fnv_windows_boot_ui_create(struct fnv_windows_process *,const struct fnv_native_image *,struct fnv_windows_boot_ui **);
enum fnv_cpu_result fnv_windows_boot_ui_destroy(struct fnv_windows_boot_ui *);
const struct fnv_cpu_export *fnv_windows_boot_ui_exports(size_t *);
enum fnv_cpu_result fnv_windows_boot_ui_class(struct fnv_windows_boot_ui *,uint32_t atom,struct fnv_boot_class *);
#endif
