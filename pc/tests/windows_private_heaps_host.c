#define ARENA 0x800000u
#define main process_heap_regression_main
#include "windows_heap_host.c"
#undef main
#include "guest_memory.h"
#include "memory_horizon.h"
#define VM_BASE (BASE+0x200000u)
#define VM_BYTES 0x400000u
enum {CREATE_HEAP=8,DESTROY_HEAP=9};
static struct fnv_guest_memory *vm;
static unsigned permission_calls,fail_at,broken_commit;
static uint32_t permissions(void *context,void *address,uint32_t bytes,uint32_t permission){
    (void)context;permission_calls++;if(permission_calls==fail_at)return 0xCE01;
    DWORD old;return VirtualProtect(address,bytes,permission==3?PAGE_READWRITE:permission==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();
}
static struct fnv_vm_permissions platform={NULL,permissions};
static struct fnv_vm_backend_result apply(void *context,const struct fnv_vm_transition *t){
    if(broken_commit && t->operation==FNV_VM_COMMIT_PAGES)return (struct fnv_vm_backend_result){FNV_VM_BROKEN,0xCE01};
    return fnv_vm_eager_apply(context,t);
}
static struct fnv_vm_counters counters(void){struct fnv_vm_counters c;fnv_vm_get_counters(vm,&c);return c;}
static DWORD WINAPI private_worker(void *context){
    uint32_t handle=(uint32_t)(uintptr_t)context;struct fnv_windows_thread_config t=thread_config(1);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    for(unsigned n=0;n<1000;++n){uint32_t p=call(ALLOC,handle,8,97,0);assert(p);memset(arena+p-BASE,0x79,97);assert(call(FREE,handle,0,p,0));
        if(!(n%32)){uint32_t temporary=call(CREATE_HEAP,0,4096,65536,0);assert(temporary && call(DESTROY_HEAP,temporary,0,0,0));}}
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_vm_config v={arena+VM_BASE-BASE,VM_BASE,VM_BYTES,VM_BYTES,&platform,apply};assert(!fnv_vm_create(&v,&vm).error);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=ARENA;
    cfg.cpu.code_low=BASE+0x1000;cfg.cpu.code_high=BASE+image.size;cfg.image_bytes=image.size;cfg.peb_address=BASE+0x8000;cfg.process_id=11;
    cfg.heap_address=HEAP_BASE;cfg.heap_bytes=HEAP_BYTES;cfg.heap_capacity=128;cfg.memory=vm;cfg.private_heap_reserve=0x100000;cfg.private_heap_limit=3;
    struct fnv_windows_process *bad_process;struct fnv_windows_config bad=cfg;bad.memory=NULL;
    assert(fnv_windows_create(&bad,&bad_process)==FNV_CPU_INVALID && !bad_process);
    bad=cfg;bad.private_heap_reserve++;assert(fnv_windows_create(&bad,&bad_process)==FNV_CPU_INVALID && !bad_process);
    assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==10);
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=t.stack_high-256;
        uint32_t flags=!strcmp(argv[2],"--broken-create")?0:4;broken_commit=flags==0;
        uint32_t args[]={0x401500,flags,0,0};for(unsigned i=0;i<4;++i)write32(s.gpr[4]+i*4,args[i]);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
        struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401600,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(strstr(fault.text,broken_commit?"rollback":"flags/SEH"));
        broken_commit=0;assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    assert(!call(CREATE_HEAP,0,0,UINT32_MAX,0) && !counters().reserved_bytes);
    fail_at=permission_calls+1;assert(!call(CREATE_HEAP,0,0,0,0));fail_at=0;
    assert(!counters().reserved_bytes && !counters().committed_bytes);
    call(SET_ERROR,0x1234,0,0,0);uint32_t first=call(CREATE_HEAP,0,0,0,0);assert(first && call(GET_ERROR,0,0,0,0)==0x1234);
    assert(counters().reserved_bytes==0x100000 && counters().committed_bytes==4096);
    uint32_t second=call(CREATE_HEAP,0,4097,0,0);assert(second && second!=first);
    assert(counters().reserved_bytes==0x200000 && counters().committed_bytes==4096+8192);
    struct fnv_windows_heap_stats metadata;assert(fnv_windows_heap_stats(process,&metadata)==FNV_CPU_OK);
    assert(metadata.live_heaps==3 && metadata.live_blocks==0 && metadata.metadata_bytes==32*16);
    uint32_t fixed=call(CREATE_HEAP,0,0,0x100000,0);assert(fixed && !call(CREATE_HEAP,0,0,0,0));
    assert(!call(ALLOC,fixed,0,0x7fff8,0) && !call(ALLOC,fixed,0,0x80000,0));
    assert(call(DESTROY_HEAP,fixed,0,0,0) && counters().reserved_bytes==0x200000);
    uint32_t unserialized=call(CREATE_HEAP,1,4096,0,0);HANDLE native_unserialized=HeapCreate(HEAP_NO_SERIALIZE,4096,0);
    assert(unserialized && native_unserialized);
    for(unsigned i=0;i<100;++i){uint32_t size=i+1,block=call(ALLOC,unserialized,1|8,size,0);
        unsigned char *native=HeapAlloc(native_unserialized,HEAP_NO_SERIALIZE|HEAP_ZERO_MEMORY,size);assert(block && native && !memcmp(arena+block-BASE,native,size));
        memset(arena+block-BASE,0x61,size);memset(native,0x61,size);
        block=call(REALLOC,unserialized,1|8,block,size+23);native=HeapReAlloc(native_unserialized,HEAP_NO_SERIALIZE|HEAP_ZERO_MEMORY,native,size+23);
        assert(block && native && !memcmp(arena+block-BASE,native,size+23));
        assert(call(HEAP_SIZE,unserialized,1,block,0)==size+23 && call(VALIDATE,unserialized,1,block,0));
        assert(call(FREE,unserialized,1,block,0) && HeapFree(native_unserialized,HEAP_NO_SERIALIZE,native));}
    assert(HeapDestroy(native_unserialized) && call(DESTROY_HEAP,unserialized,0,0,0));
    HANDLE native_heap=HeapCreate(0,0,0);assert(native_heap);unsigned seed=0x53913,sequences=0;
    for(unsigned n=0;n<500;++n){seed=seed*1664525u+1013904223u;uint32_t bytes=seed%8192,heap=n&1?first:second;
        uint32_t p=call(ALLOC,heap,8,bytes,0);unsigned char *native=HeapAlloc(native_heap,HEAP_ZERO_MEMORY,bytes);assert(p && native && !(p&7));
        assert(!memcmp(arena+p-BASE,native,bytes));memset(arena+p-BASE,0x2b,bytes);memset(native,0x2b,bytes);
        uint32_t q=call(REALLOC,heap,8,p,bytes+77);native=HeapReAlloc(native_heap,HEAP_ZERO_MEMORY,native,bytes+77);assert(q && native && !memcmp(arena+q-BASE,native,bytes+77));
        assert(call(HEAP_SIZE,heap,0,q,0)==bytes+77 && HeapSize(native_heap,0,native)>=bytes+77);
        assert(call(FREE,heap,0,q,0) && HeapFree(native_heap,0,native));sequences++;}
    assert(HeapDestroy(native_heap));
    uint32_t block=call(ALLOC,first,8,64,0);assert(block);memset(arena+block-BASE,0x53,64);
    assert(!call(FREE,second,0,block,0) && !call(FREE,HEAP_BASE,0,block,0) && call(VALIDATE,first,0,block,0));
    assert(call(HEAP_SIZE,second,0,block,0)==UINT32_MAX && !call(REALLOC,second,8,block,128));
    struct fnv_vm_counters before=counters();fail_at=permission_calls+1;
    assert(!call(REALLOC,first,8,block,0x80000));fail_at=0;
    assert(counters().committed_bytes==before.committed_bytes && call(HEAP_SIZE,first,0,block,0)==64 && arena[block-BASE]==0x53);
    assert(call(REALLOC,first,8,block,0x80000)==block && arena[block-BASE+64]==0);
    assert(!call(DESTROY_HEAP,HEAP_BASE,0,0,0));
    fail_at=permission_calls+1;assert(!call(DESTROY_HEAP,first,0,0,0));fail_at=0;assert(call(VALIDATE,first,0,block,0));
    assert(call(FREE,first,0,block,0));
    HANDLE other=CreateThread(NULL,0,private_worker,(void *)(uintptr_t)first,0,NULL);assert(other);
    for(unsigned i=0;i<1000;++i){uint32_t p=call(ALLOC,first,8,64,0);assert(p);memset(arena+p-BASE,0x42,64);assert(call(FREE,first,0,p,0));}
    assert(WaitForSingleObject(other,INFINITE)==WAIT_OBJECT_0);DWORD exit_code;assert(GetExitCodeThread(other,&exit_code) && !exit_code);CloseHandle(other);
    assert(call(ALLOC,first,0,128,0));assert(call(DESTROY_HEAP,first,0,0,0));
    assert(counters().reserved_bytes==0x100000 && !call(VALIDATE,first,0,0,0) && !call(ALLOC,first,0,8,0));
    assert(call(VALIDATE,second,0,0,0) && call(PROCESS_HEAP,0,0,0,0)==HEAP_BASE);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_BUSY);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && call(DESTROY_HEAP,second,0,0,0));
    assert(!counters().reserved_bytes && !counters().committed_bytes);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && !fnv_vm_destroy(vm).error);
    assert(VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"private_heap_sequences\":%u,\"no_serialize_sequences\":100,\"empty_private_metadata_bytes\":0,\"concurrent_cycles\":2000,\"iat_imports\":10,\"vm_growth_and_release\":true,\"failed_commit_and_destroy_preserve_heap\":true,\"all_reservations_released\":true,\"native_engine_started\":false}\n",sequences);return 0;
}
