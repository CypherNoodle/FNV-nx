#ifndef FNV_WINDOWS_MODULES_H
#define FNV_WINDOWS_MODULES_H
#include "windows_thread.h"
#define FNV_WINDOWS_MODULE_EXPORTS 6u
struct fnv_windows_modules;
/* Prepared, pinned native providers, not a loader for Windows DLL machine code.
 * Single CPU process. Create before first attach, destroy after all detach and
 * before Windows process destruction. Catalog strings/functions are copied.
 * Caller serializes setup/teardown and retains native function code until destroy.
 * Include windows_exports and modules_exports in catalog for dynamic lookups.
 * For windows_bind_imports pass only the non-built-in exports (including the
 * module APIs); that binder adds windows_exports itself. */
enum fnv_cpu_result fnv_windows_modules_create(struct fnv_windows_process *,
    const struct fnv_native_image *,const char *exe_name,const struct fnv_cpu_export *catalog,size_t count,
    struct fnv_windows_modules **);
enum fnv_cpu_result fnv_windows_modules_destroy(struct fnv_windows_modules *);
const struct fnv_cpu_export *fnv_windows_modules_exports(size_t *count);
struct fnv_windows_module_stats {size_t pinned_modules,native_exports;uint64_t load_calls,dynamic_references;};
/* Caller retains the module provider; setup/teardown is externally serialized. */
enum fnv_cpu_result fnv_windows_modules_stats(struct fnv_windows_modules *,struct fnv_windows_module_stats *);
#endif
