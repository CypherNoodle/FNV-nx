#ifndef FNV_MEMORY_HORIZON_H
#define FNV_MEMORY_HORIZON_H
#include "guest_memory.h"
/* Eager physical backing with real page permissions; decommit retains the
 * owned backing, which is zeroed on recommit. Not a sparse RAM allocator.
 * setter must change one entire range atomically or leave it unchanged. */
struct fnv_vm_permissions {
    void *context;
    uint32_t (*set)(void *,void *address,uint32_t bytes,uint32_t permission);
};
struct fnv_vm_backend_result fnv_vm_eager_apply(void *,const struct fnv_vm_transition *);
#ifdef __SWITCH__
void fnv_vm_horizon_permissions(struct fnv_vm_permissions *);
#endif
#endif
