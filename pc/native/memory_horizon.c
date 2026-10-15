#include "memory_horizon.h"
#include <string.h>
#ifdef __SWITCH__
#include <switch/kernel/svc.h>
static uint32_t horizon_set(void *unused,void *at,uint32_t bytes,uint32_t permission) {
    (void)unused;return svcSetMemoryPermission(at,bytes,permission);
}
void fnv_vm_horizon_permissions(struct fnv_vm_permissions *p) {
    if(p){p->context=NULL;p->set=horizon_set;}
}
#endif
static uint32_t permission(uint32_t p) { return p==4?3:p==2?1:0; }
struct fnv_vm_backend_result fnv_vm_eager_apply(void *context,const struct fnv_vm_transition *t) {
    struct fnv_vm_permissions *p=context;
    if(!p || !p->set || !t || !t->bytes || (t->bytes&4095) || ((uintptr_t)t->address&4095))
        return (struct fnv_vm_backend_result){FNV_VM_REJECTED,87};
    if(t->operation>FNV_VM_RESTORE_BACKING ||
       (t->protection!=1 && t->protection!=2 && t->protection!=4) ||
       (t->operation==FNV_VM_COMMIT_PAGES && !t->before))
        return (struct fnv_vm_backend_result){FNV_VM_REJECTED,87};
    uint32_t target=t->operation==FNV_VM_INITIALIZE || t->operation==FNV_VM_HIDE_PAGES?0:permission(t->protection);
    int commit=t->operation==FNV_VM_COMMIT_PAGES;
    uint32_t error=p->set(p->context,t->address,t->bytes,commit?3:target);
    if(error)return (struct fnv_vm_backend_result){FNV_VM_REJECTED,error};
    if(commit) {
        for(uint32_t i=0;i<t->bytes/4096;) {
            if(t->before[i].protection){i++;continue;}
            uint32_t end=i+1;
            while(end<t->bytes/4096 && !t->before[end].protection)end++;
            memset(t->address+(size_t)i*4096,0,(size_t)(end-i)*4096);i=end;
        }
        if(target!=3)error=p->set(p->context,t->address,t->bytes,target);
        if(error) {
            int broken=0;
            for(uint32_t i=0;i<t->bytes/4096;) {
                uint32_t perm=permission(t->before[i].protection),end=i+1;
                while(end<t->bytes/4096 && permission(t->before[end].protection)==perm)end++;
                if(p->set(p->context,t->address+(size_t)i*4096,(end-i)*4096,perm))broken=1;
                i=end;
            }
            return (struct fnv_vm_backend_result){broken?FNV_VM_BROKEN:FNV_VM_REJECTED,error};
        }
    }
    return (struct fnv_vm_backend_result){FNV_VM_APPLIED,0};
}
