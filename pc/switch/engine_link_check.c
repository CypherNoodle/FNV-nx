/* Link-only entry. Never packaged/delivered as an engine NRO.
 * Taking the loader address pulls the real Mesa implementation into the ELF.
 * The linker roots all public CPU/runtime definitions, including providers not
 * reachable from this deliberately non-executing entry. Unused sections of
 * external archives remain eligible for normal platform garbage collection. */
#include <vulkan/vulkan_core.h>
#include "engine_layout.h"
#include "engine_process_plan.h"
static PFN_vkGetInstanceProcAddr volatile loader;
static int (*volatile plan_builder)(uint32_t,uint32_t,unsigned char *,size_t,
    struct fnv_vm_permissions *,struct fnv_engine_process_plan *);
int main(void) {
    struct fnv_engine_layout layout;
    loader=vkGetInstanceProcAddr;
    plan_builder=fnv_engine_process_plan_create;
    return !fnv_engine_layout_create(0x10000,0x1000,&layout);
}
