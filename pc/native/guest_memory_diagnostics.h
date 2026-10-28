#ifndef FNV_GUEST_MEMORY_DIAGNOSTICS_H
#define FNV_GUEST_MEMORY_DIAGNOSTICS_H
#include "guest_memory.h"
/* Returns zero on contention/invalid input without modifying output. */
int fnv_vm_try_counters(struct fnv_guest_memory *,struct fnv_vm_counters *);
#endif
