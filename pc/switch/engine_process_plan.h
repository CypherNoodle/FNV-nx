#ifndef FNV_SWITCH_ENGINE_PROCESS_PLAN_H
#define FNV_SWITCH_ENGINE_PROCESS_PLAN_H
#include "engine_layout.h"
#include "windows_thread.h"
#include "memory_horizon.h"
struct fnv_engine_process_plan {
    struct fnv_engine_layout layout;
    struct fnv_vm_config vm;
    struct fnv_windows_config process;
    struct fnv_windows_thread_config main_thread;
};
/* Pure configuration, no allocation/permissions changes or guest execution.
 * Caller retains page-aligned real arena backing and permission callbacks.
 * After loading/identifying the PE and overlaying its verified capture:
 * zero the dedicated non-image regions, create plan.vm, assign process.memory,
 * create the process, register providers and bind IAT/TLS, then initialize
 * main_thread.state with fnv_cpu_state_init before attaching it. */
int fnv_engine_process_plan_create(uint32_t base,uint32_t image_bytes,
    unsigned char *arena,size_t arena_bytes,struct fnv_vm_permissions *,
    struct fnv_engine_process_plan *);
#endif
