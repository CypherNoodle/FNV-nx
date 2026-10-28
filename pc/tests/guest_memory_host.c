/* Windows is the contract oracle; only test-owned memory is allocated. */
#ifndef _WIN32
#error This contract oracle requires Windows
#endif
#include <windows.h>
#include "guest_memory.h"
#include "guest_memory_diagnostics.h"
#include "memory_horizon.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POOL (256u*1024*1024)
#define BASE 0x2000000u
struct permissions { unsigned calls,fail_at,fail_again; };
static struct permissions permissions;
static unsigned char *backing;
static struct fnv_guest_memory *vm;
static unsigned queries,operations,snapshot_checks;static int check_snapshot;
static uint32_t native_set(void *opaque,void *address,uint32_t bytes,uint32_t permission) {
    struct permissions *p=opaque;p->calls++;
    if(check_snapshot){struct fnv_vm_counters out,saved;memset(&out,0xa5,sizeof(out));saved=out;assert(!fnv_vm_try_counters(vm,&out) && !memcmp(&out,&saved,sizeof(out)));snapshot_checks++;}
    if(p->calls==p->fail_at || p->calls==p->fail_again)return 0xCE01;
    DWORD previous;DWORD protect=permission==3?PAGE_READWRITE:permission==1?PAGE_READONLY:PAGE_NOACCESS;
    if(!VirtualProtect(address,bytes,protect,&previous))return GetLastError();
    return 0;
}
static struct fnv_vm_permissions backend={&permissions,native_set};
static struct fnv_vm_information info(uint32_t at) {
    struct fnv_vm_information i;assert(fnv_vm_query(vm,at,&i).value==28);return i;
}
static void compare(uint32_t guest,unsigned char *oracle,uint32_t base) {
    struct fnv_vm_information i=info(guest);MEMORY_BASIC_INFORMATION n;
    assert(VirtualQuery(oracle+(guest-base),&n,sizeof(n))==sizeof(n));
    assert(i.state==n.State);queries++;
    if(n.State!=MEM_FREE) {
        assert(i.base==guest/4096*4096);
        assert(i.allocation_base==base+(uint32_t)((unsigned char*)n.AllocationBase-oracle));
        assert(i.allocation_protect==n.AllocationProtect && i.protect==n.Protect && i.type==n.Type);
        assert(i.size==n.RegionSize);
        if(i.protect==2 || i.protect==4)
            assert(!memcmp(backing+(guest-BASE),oracle+(guest-base),4096-(guest&4095)));
    }
}
static void initialize(uint32_t limit) {
    struct fnv_vm_config c={backing,BASE,POOL,limit,&backend,fnv_vm_eager_apply};
    assert(!fnv_vm_create(&c,&vm).error);
}
static void original_sizes(void) {
    uint32_t reserve=200u<<20,commit=85u<<20;
    struct fnv_vm_result r=fnv_vm_allocate(vm,0,reserve,FNV_VM_RESERVE,4);assert(r.value==BASE);
    unsigned char *oracle=VirtualAlloc(NULL,reserve,MEM_RESERVE,PAGE_READWRITE);assert(oracle);
    compare(BASE,oracle,BASE);compare(BASE+reserve-4096,oracle,BASE);
    assert(fnv_vm_allocate(vm,BASE,commit,FNV_VM_COMMIT,4).value==BASE);
    assert(VirtualAlloc(oracle,commit,MEM_COMMIT,PAGE_READWRITE)==oracle);
    assert(!memcmp(backing,oracle,commit));
    for(uint32_t i=0;i<commit;i++)assert(backing[i]==0);
    memcpy(backing+4095,"abcd",4);memcpy(oracle+4095,"abcd",4);
    assert(fnv_vm_allocate(vm,BASE,8192,FNV_VM_COMMIT,4).value==BASE);
    assert(VirtualAlloc(oracle,8192,MEM_COMMIT,PAGE_READWRITE)==oracle);
    assert(!memcmp(backing,oracle,8192));
    /* Recommit may change protection, without clearing existing contents. */
    assert(fnv_vm_allocate(vm,BASE,4096,FNV_VM_COMMIT,2).value==BASE);
    assert(VirtualAlloc(oracle,4096,MEM_COMMIT,PAGE_READONLY)==oracle);compare(BASE,oracle,BASE);
    assert(fnv_vm_free(vm,BASE+4095,2,FNV_VM_DECOMMIT).value);
    assert(VirtualFree(oracle+4095,2,MEM_DECOMMIT));compare(BASE,oracle,BASE);compare(BASE+4096,oracle,BASE);
    assert(fnv_vm_free(vm,BASE,8192,FNV_VM_DECOMMIT).value && VirtualFree(oracle,8192,MEM_DECOMMIT));
    assert(fnv_vm_allocate(vm,BASE,8192,FNV_VM_COMMIT,4).value==BASE);
    assert(VirtualAlloc(oracle,8192,MEM_COMMIT,PAGE_READWRITE)==oracle);compare(BASE,oracle,BASE);
    for(unsigned i=0;i<8192;i++)assert(backing[i]==0);
    assert(!fnv_vm_free(vm,BASE+4096,0,FNV_VM_RELEASE).value && !VirtualFree(oracle+4096,0,MEM_RELEASE));
    assert(!fnv_vm_free(vm,BASE,1,FNV_VM_RELEASE).value && !VirtualFree(oracle,1,MEM_RELEASE));
    assert(fnv_vm_free(vm,BASE,0,FNV_VM_DECOMMIT).value && VirtualFree(oracle,0,MEM_DECOMMIT));
    compare(BASE,oracle,BASE);
    assert(fnv_vm_free(vm,BASE,0,FNV_VM_RELEASE).value && VirtualFree(oracle,0,MEM_RELEASE));
    assert(info(BASE).state==FNV_VM_FREE);
    struct fnv_vm_counters c;fnv_vm_get_counters(vm,&c);assert(!c.reserved_bytes && !c.committed_bytes);
}
static uint32_t random_state=0x91CD337Fu;
static uint32_t random32(void) {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void randomized(void) {
    uint32_t base=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE,4).value;assert(base);
    unsigned char *oracle=VirtualAlloc(NULL,65536,MEM_RESERVE,PAGE_READWRITE);assert(oracle);
    for(unsigned n=0;n<1600;n++) {
        uint32_t first=random32()%16,count=1+random32()%(16-first),op=random32()%3;
        uint32_t protection=(uint32_t[]){1,2,4}[random32()%3],at=base+first*4096;
        struct fnv_vm_result r;
        if(op==0){r=fnv_vm_allocate(vm,at,count*4096,FNV_VM_COMMIT,protection);
            assert(r.value==at && VirtualAlloc(oracle+first*4096,count*4096,MEM_COMMIT,protection)==oracle+first*4096);}
        else if(op==1){r=fnv_vm_free(vm,at,count*4096,FNV_VM_DECOMMIT);
            assert(r.value && VirtualFree(oracle+first*4096,count*4096,MEM_DECOMMIT));}
        else {uint32_t old=0;DWORD native_old=0;
            r=fnv_vm_protect(vm,at,count*4096,protection,&old);
            int ok=VirtualProtect(oracle+first*4096,count*4096,protection,&native_old);
            assert(!!r.value==!!ok);if(ok)assert(old==native_old);}
        operations++;
        for(unsigned i=0;i<16;i++) {
            compare(base+i*4096,oracle,base);
            if(info(base+i*4096).protect==4){uint32_t value=random32();
                memcpy(backing+(base-BASE)+i*4096,&value,4);memcpy(oracle+i*4096,&value,4);}
        }
    }
    assert(fnv_vm_free(vm,base,0,FNV_VM_RELEASE).value && VirtualFree(oracle,0,MEM_RELEASE));
}
static void boundaries(void) {
    unsigned char *oracle=VirtualAlloc(NULL,65536,MEM_RESERVE,PAGE_READWRITE);assert(oracle);
    assert(VirtualFree(oracle,0,MEM_RELEASE));
    oracle=VirtualAlloc(oracle+0x2FFF,2,MEM_RESERVE,PAGE_READWRITE);assert(oracle);
    uint32_t base=fnv_vm_allocate(vm,BASE+0x2FFF,2,FNV_VM_RESERVE,4).value;assert(base==BASE);
    compare(BASE,oracle,BASE);compare(BASE+0x2000,oracle,BASE);
    assert(info(BASE).size==0x4000);
    assert(fnv_vm_free(vm,base,0,FNV_VM_RELEASE).value && VirtualFree(oracle,0,MEM_RELEASE));
    assert(fnv_vm_allocate(vm,BASE,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value==BASE);
    assert(fnv_vm_allocate(vm,BASE+65536,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value==BASE+65536);
    assert(!fnv_vm_allocate(vm,BASE+65535,2,FNV_VM_COMMIT,4).value);
    uint32_t old;assert(!fnv_vm_protect(vm,BASE+65535,2,2,&old).value);
    assert(info(BASE).size==65536 && info(BASE+65536).allocation_base==BASE+65536);
    assert(fnv_vm_free(vm,BASE,0,FNV_VM_RELEASE).value && fnv_vm_free(vm,BASE+65536,0,FNV_VM_RELEASE).value);
}
static HANDLE concurrent_start;
static DWORD WINAPI memory_worker(void *opaque) {
    uint32_t tag=(uint32_t)(uintptr_t)opaque;
    assert(WaitForSingleObject(concurrent_start,INFINITE)==WAIT_OBJECT_0);
    for(unsigned round=0;round<250;round++) {
        uint32_t at=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;
        assert(at);
        uint32_t *words=(uint32_t*)(backing+(at-BASE));
        for(unsigned i=0;i<65536/4;i++)words[i]=tag^(round*65537u)^i;
        struct fnv_vm_information region;
        assert(fnv_vm_query(vm,at,&region).value==28);
        assert(region.allocation_base==at && region.size==65536 && region.protect==4);
        uint32_t old;
        assert(fnv_vm_protect(vm,at,65536,2,&old).value && old==4);
        for(unsigned i=0;i<65536/4;i++)assert(words[i]==(tag^(round*65537u)^i));
        assert(fnv_vm_free(vm,at,0,FNV_VM_RELEASE).value);
    }
    return 0;
}
static void concurrent(void) {
    concurrent_start=CreateEventW(NULL,TRUE,FALSE,NULL);assert(concurrent_start);
    HANDLE threads[2]={CreateThread(NULL,0,memory_worker,(void*)(uintptr_t)0x12340000u,0,NULL),
        CreateThread(NULL,0,memory_worker,(void*)(uintptr_t)0xABCD0000u,0,NULL)};
    assert(threads[0] && threads[1] && SetEvent(concurrent_start));
    assert(WaitForMultipleObjects(2,threads,TRUE,INFINITE)==WAIT_OBJECT_0);
    for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    CloseHandle(concurrent_start);
    struct fnv_vm_counters counters;fnv_vm_get_counters(vm,&counters);
    assert(!counters.reserved_bytes && !counters.committed_bytes);
}
static void failures(void) {
    unsigned calls=permissions.calls;
    assert(fnv_vm_allocate(vm,0,1,0x80000,4).error==50);
    assert(fnv_vm_allocate(vm,0,1,FNV_VM_COMMIT,0x40).error==50);
    assert(fnv_vm_allocate(vm,0,0,FNV_VM_RESERVE,4).error==87);
    assert(permissions.calls==calls);
    uint32_t base=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE,4).value;assert(base);
    assert(!fnv_vm_allocate(vm,base+65535,2,FNV_VM_COMMIT,4).value);
    assert(info(base).state==FNV_VM_RESERVE);
    assert(fnv_vm_allocate(vm,base,4096,FNV_VM_COMMIT,2).value);
    assert(fnv_vm_allocate(vm,base+4096,4096,FNV_VM_COMMIT,4).value);
    struct fnv_vm_information before[3];for(unsigned i=0;i<3;i++)before[i]=info(base+i*4096);
    memcpy(backing+(base-BASE)+4096,"preserved",10);
    permissions.fail_at=permissions.calls+2;
    struct fnv_vm_result r=fnv_vm_allocate(vm,base,12288,FNV_VM_COMMIT,2);
    assert(r.error==8 && r.native_error==0xCE01);
    for(unsigned i=0;i<3;i++){struct fnv_vm_information after=info(base+i*4096);assert(!memcmp(before+i,&after,sizeof(after)));}
    assert(!memcmp(backing+(base-BASE)+4096,"preserved",10));
    MEMORY_BASIC_INFORMATION host;
    assert(VirtualQuery(backing+(base-BASE),&host,sizeof(host)) && host.Protect==PAGE_READONLY);
    assert(VirtualQuery(backing+(base-BASE)+8192,&host,sizeof(host)) && host.Protect==PAGE_NOACCESS);
    permissions.fail_at=0;
    assert(fnv_vm_free(vm,base,0,FNV_VM_RELEASE).value);
    /* A failed first permission change must not create a reservation. */
    permissions.fail_at=permissions.calls+1;
    assert(fnv_vm_allocate(vm,base,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,4).error==8);
    assert(info(base).state==FNV_VM_FREE);permissions.fail_at=0;
    assert(fnv_vm_retain(vm));assert(fnv_vm_destroy(vm).error==170);fnv_vm_release_reference(vm);
    base=fnv_vm_allocate(vm,0,200u<<20,FNV_VM_RESERVE,4).value;assert(base);
    calls=permissions.calls;
    assert(fnv_vm_allocate(vm,base,101u<<20,FNV_VM_COMMIT,4).error==8);
    assert(permissions.calls==calls && info(base).state==FNV_VM_RESERVE);
    assert(fnv_vm_free(vm,base,0,FNV_VM_RELEASE).value);
    base=fnv_vm_allocate(vm,0,12288,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(base);
    permissions.fail_at=permissions.calls+2;permissions.fail_again=permissions.calls+3;
    assert(fnv_vm_allocate(vm,base,12288,FNV_VM_COMMIT,2).error==FNV_VM_CORRUPT);
    struct fnv_vm_information unused;
    assert(fnv_vm_query(vm,base,&unused).error==FNV_VM_CORRUPT);
}
static uintptr_t fault_low,fault_high;
static LONG WINAPI access_violation(EXCEPTION_POINTERS *p) {
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
       p->ExceptionRecord->ExceptionInformation[1]>=fault_low && p->ExceptionRecord->ExceptionInformation[1]<fault_high)_Exit(77);
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char **argv) {
    backing=VirtualAlloc(NULL,POOL,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(backing);
    initialize(100u<<20);
    if(argc==2) {
        assert(AddVectoredExceptionHandler(1,access_violation));fault_low=(uintptr_t)backing;fault_high=fault_low+POOL;
        if(!strcmp(argv[1],"--readonly")){assert(fnv_vm_allocate(vm,BASE,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,2).value);*(volatile unsigned char*)backing=1;}
        else {volatile unsigned char value=*(volatile unsigned char*)backing;(void)value;}
        return 9;
    }
    check_snapshot=1;original_sizes();boundaries();randomized();concurrent();failures();check_snapshot=0;assert(snapshot_checks);
    struct fnv_vm_counters counters;assert(fnv_vm_try_counters(vm,&counters));
    permissions.fail_at=permissions.fail_again=0;assert(!fnv_vm_destroy(vm).error);
    assert(VirtualFree(backing,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"randomized_operations\":%u,\"native_query_comparisons\":%u,\"concurrent_rounds\":500,\"original_reserve_mb\":200,\"original_commit_mb\":85,\"rollback_failure_poisoned\":true}\n",operations,queries);return 0;
}
