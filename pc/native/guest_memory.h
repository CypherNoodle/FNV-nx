#ifndef FNV_GUEST_MEMORY_H
#define FNV_GUEST_MEMORY_H
#include <stddef.h>
#include <stdint.h>

#define FNV_VM_PAGE 4096u
#define FNV_VM_GRANULARITY 65536u
#define FNV_VM_RESERVE 0x2000u
#define FNV_VM_COMMIT 0x1000u
#define FNV_VM_DECOMMIT 0x4000u
#define FNV_VM_RELEASE 0x8000u
#define FNV_VM_FREE 0x10000u
#define FNV_VM_PRIVATE 0x20000u
#define FNV_VM_NOACCESS 1u
#define FNV_VM_READONLY 2u
#define FNV_VM_READWRITE 4u
#define FNV_VM_CORRUPT 0xE0000001u
struct fnv_guest_memory;
struct fnv_vm_result { uint32_t value,error,native_error; };
struct fnv_vm_information {
    uint32_t base,allocation_base,allocation_protect,size,state,protect,type;
};
/* Read-only old page state provided to the backend during a transaction.
 * protection==0 means uncommitted. Execute/guard/COW pages are unsupported. */
struct fnv_vm_page { uint32_t owner,protection,allocation_pages,allocation_protect; };
enum fnv_vm_operation { FNV_VM_INITIALIZE,FNV_VM_COMMIT_PAGES,FNV_VM_HIDE_PAGES,FNV_VM_PROTECT_PAGES,FNV_VM_RESTORE_BACKING };
struct fnv_vm_transition {
    enum fnv_vm_operation operation;
    unsigned char *address;
    uint32_t bytes,protection;
    const struct fnv_vm_page *before;
};
enum fnv_vm_backend_status { FNV_VM_APPLIED,FNV_VM_REJECTED,FNV_VM_BROKEN };
struct fnv_vm_backend_result { enum fnv_vm_backend_status status;uint32_t native_error; };
struct fnv_vm_config {
    unsigned char *backing;
    uint32_t base,bytes,commit_limit;
    void *context;
    /* Called under the VM lock: no reentry, longjmp or exceptions. REJECTED
     * must preserve previous permissions and committed bytes; otherwise BROKEN.
     * COMMIT must zero newly committed pages and preserve existing contents. */
    struct fnv_vm_backend_result (*apply)(void *,const struct fnv_vm_transition *);
};
struct fnv_vm_counters { uint32_t reserved_bytes,committed_bytes,commit_limit,poisoned; };
struct fnv_vm_result fnv_vm_create(const struct fnv_vm_config *,struct fnv_guest_memory **);
struct fnv_vm_result fnv_vm_destroy(struct fnv_guest_memory *);
int fnv_vm_retain(struct fnv_guest_memory *);
void fnv_vm_release_reference(struct fnv_guest_memory *);
int fnv_vm_binding(const struct fnv_guest_memory *,unsigned char **,uint32_t *,uint32_t *);
struct fnv_vm_result fnv_vm_allocate(struct fnv_guest_memory *,uint32_t address,uint32_t bytes,uint32_t flags,uint32_t protection);
struct fnv_vm_result fnv_vm_free(struct fnv_guest_memory *,uint32_t address,uint32_t bytes,uint32_t flags);
struct fnv_vm_result fnv_vm_query(struct fnv_guest_memory *,uint32_t address,struct fnv_vm_information *);
struct fnv_vm_result fnv_vm_protect(struct fnv_guest_memory *,uint32_t address,uint32_t bytes,uint32_t protection,uint32_t *old_protection);
int fnv_vm_access(struct fnv_guest_memory *,uint32_t address,uint32_t bytes,int write_access);
void fnv_vm_get_counters(struct fnv_guest_memory *,struct fnv_vm_counters *);
#endif
