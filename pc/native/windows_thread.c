#if !defined(_WIN32) && !defined(__SWITCH__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* Original PC bridges; Windows ABI/data layout references are documented in
 * docs/native-windows-thread.md. No Wine source or DLLs are incorporated. */
#include "fnv_sdk_runtime.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include "host_condition.h"
#include "host_clock.h"
#include <stdatomic.h>
#include <stdlib.h>

enum { TEB_ERROR=0x34, TEB_SLOTS=0xE10, TEB_EXPANSION=0xF94 };
enum { ERROR_INVALID_PARAMETER_VALUE=87, ERROR_NO_MORE_ITEMS_VALUE=259 };
struct windows_thread {
    struct fnv_windows_process *process;
    struct windows_thread *next;
    struct fnv_windows_thread_config config;
    unsigned char *teb, *expansion;
};
struct heap_block {uint32_t address,bytes,span;};
struct heap_store {
    struct heap_block *blocks;uint32_t count,capacity,first_gap,address,bytes,committed,fixed,options;
    uint64_t find_steps,allocation_steps,gap_steps;
    struct heap_store *next;
};
struct critical_section {
    uint32_t address,owner,recursion,waiters,spin;
    fnv_host_condition condition;
};
struct event_waiter {struct event_waiter *next;uint32_t released;};
struct kernel_object {
    uint32_t generation,live,manual,signaled,waiters,free_next;
    struct event_waiter *waiting;
    fnv_host_condition condition;
};
struct fnv_windows_process {
    struct fnv_windows_config config;
    struct fnv_cpu_runtime *cpu;
    struct windows_thread *threads;
    uint32_t allocated[FNV_WINDOWS_TLS_SLOTS/32];
    atomic_flag guard;
    fnv_host_mutex heap_guard;
    struct heap_store heap,*private_heaps;
    uint32_t private_heap_count,heap_native_fault;
    fnv_host_mutex sync_guard;
    struct critical_section *sections;
    uint64_t sync_waits,sync_wakes,sync_try_failures;
    fnv_host_mutex kernel_guard;
    struct kernel_object *handles;
    uint32_t kernel_free;
    uint64_t kernel_waits,kernel_timeouts,kernel_sets,kernel_resets;
};
static RECOMP_TLS struct windows_thread current;
static void lock(struct fnv_windows_process *p) {
    while(atomic_flag_test_and_set_explicit(&p->guard,memory_order_acquire)) {}
}
static void unlock(struct fnv_windows_process *p) {
    atomic_flag_clear_explicit(&p->guard,memory_order_release);
}
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void write32(unsigned char *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));
}
static int inside(const struct fnv_windows_config *c,uint32_t at,uint32_t bytes) {
    uint64_t end=(uint64_t)c->cpu.guest_base+c->cpu.memory_bytes;
    return at>=c->cpu.guest_base && at<=end && bytes<=end-at;
}
static int overlap(uint32_t a,uint32_t n,uint32_t b,uint32_t m) {
    return (uint64_t)a+n>b && (uint64_t)b+m>a;
}
static unsigned char *memory(struct fnv_windows_process *p,uint32_t at) {
    return p->config.cpu.memory+(at-p->config.cpu.guest_base);
}
/* No operation below may longjmp while holding guard. Validate guest args
 * and mutable TEB pointers first; slots themselves use prevalidated arenas. */
static struct windows_thread *thread(void) {
    if(!current.process || g_fs_base!=current.config.teb_address) {
        recomp_unimpl("Windows thread/FS context unavailable",g_fs_base);
        abort();
    }
    if(read32(current.teb+TEB_EXPANSION)!=current.config.tls_expansion_address) {
        recomp_unimpl("Windows TLS expansion pointer was replaced",g_fs_base+TEB_EXPANSION);
        abort();
    }
    return &current;
}
static unsigned char *slot(struct windows_thread *t,uint32_t index) {
    return index<64?t->teb+TEB_SLOTS+index*4:t->expansion+(index-64)*4;
}
struct fnv_guest_memory *fnv_windows_current_memory(void) { return thread()->process->config.memory; }
void fnv_windows_set_last_error(uint32_t value) { write32(thread()->teb+TEB_ERROR,value); }
static int pool_overlap(struct fnv_windows_process *p,uint32_t address,uint32_t bytes) {
    unsigned char *backing;uint32_t base,size;
    return fnv_vm_binding(p->config.memory,&backing,&base,&size) && overlap(address,bytes,base,size);
}
static void api_get_error(void) {
    struct windows_thread *t=thread();
    fnv_cpu_return_u32(read32(t->teb+TEB_ERROR),0);
}
static void api_set_error(void) {
    uint32_t n=fnv_cpu_argument_u32(0);struct windows_thread *t=thread();
    write32(t->teb+TEB_ERROR,n);fnv_cpu_return_u32(g_eax,4);
}
static void api_thread_id(void) {
    struct windows_thread *t=thread();fnv_cpu_return_u32(read32(t->teb+0x24),0);
}
static void api_process_id(void) {
    struct windows_thread *t=thread();fnv_cpu_return_u32(read32(t->teb+0x20),0);
}
static void api_tls_alloc(void) {
    struct windows_thread *t=thread();struct fnv_windows_process *p=t->process;
    uint32_t index;
    lock(p);
    for(index=0;index<FNV_WINDOWS_TLS_SLOTS;index++) {
        uint32_t bit=1u<<(index&31);
        if(!(p->allocated[index/32]&bit)) {
            p->allocated[index/32]|=bit;
            for(struct windows_thread *other=p->threads;other;other=other->next)
                write32(slot(other,index),0);
            break;
        }
    }
    unlock(p);
    if(index==FNV_WINDOWS_TLS_SLOTS) {
        index=UINT32_MAX;write32(t->teb+TEB_ERROR,ERROR_NO_MORE_ITEMS_VALUE);
    }
    fnv_cpu_return_u32(index,0);
}
static void api_tls_free(void) {
    uint32_t index=fnv_cpu_argument_u32(0),result=0;
    struct windows_thread *t=thread();struct fnv_windows_process *p=t->process;
    lock(p);
    if(index<FNV_WINDOWS_TLS_SLOTS && (p->allocated[index/32]&(1u<<(index&31)))) {
        p->allocated[index/32]&=~(1u<<(index&31));result=1;
        for(struct windows_thread *other=p->threads;other;other=other->next)
            write32(slot(other,index),0);
    }
    unlock(p);
    if(!result)write32(t->teb+TEB_ERROR,ERROR_INVALID_PARAMETER_VALUE);
    fnv_cpu_return_u32(result,4);
}
static void api_tls_get(void) {
    uint32_t index=fnv_cpu_argument_u32(0),value=0;
    struct windows_thread *t=thread();struct fnv_windows_process *p=t->process;
    lock(p);
    if(index<FNV_WINDOWS_TLS_SLOTS)value=read32(slot(t,index));
    unlock(p);
    write32(t->teb+TEB_ERROR,index<FNV_WINDOWS_TLS_SLOTS?0:ERROR_INVALID_PARAMETER_VALUE);
    fnv_cpu_return_u32(value,4);
}
static void api_tls_set(void) {
    uint32_t index=fnv_cpu_argument_u32(0),value=fnv_cpu_argument_u32(1);
    struct windows_thread *t=thread();struct fnv_windows_process *p=t->process;
    lock(p);
    if(index<FNV_WINDOWS_TLS_SLOTS)write32(slot(t,index),value);
    unlock(p);
    if(index>=FNV_WINDOWS_TLS_SLOTS)write32(t->teb+TEB_ERROR,ERROR_INVALID_PARAMETER_VALUE);
    fnv_cpu_return_u32(index<FNV_WINDOWS_TLS_SLOTS,8);
}
#include "windows_heap.inc"
#include "windows_sync.inc"
#include "windows_events.inc"
static const struct fnv_cpu_export exports[FNV_WINDOWS_THREAD_EXPORTS]={
    {"kernel32.dll","TlsAlloc",0,0,api_tls_alloc},
    {"kernel32.dll","TlsFree",0,0,api_tls_free},
    {"kernel32.dll","TlsGetValue",0,0,api_tls_get},
    {"kernel32.dll","TlsSetValue",0,0,api_tls_set},
    {"kernel32.dll","GetLastError",0,0,api_get_error},
    {"kernel32.dll","SetLastError",0,0,api_set_error},
    {"kernel32.dll","GetCurrentThreadId",0,0,api_thread_id},
    {"kernel32.dll","GetCurrentProcessId",0,0,api_process_id},
    {"kernel32.dll","GetProcessHeap",0,0,api_process_heap},
    {"kernel32.dll","HeapAlloc",0,0,api_heap_alloc},
    {"kernel32.dll","HeapReAlloc",0,0,api_heap_realloc},
    {"kernel32.dll","HeapFree",0,0,api_heap_free},
    {"kernel32.dll","HeapSize",0,0,api_heap_size},
    {"kernel32.dll","HeapValidate",0,0,api_heap_validate},
    {"kernel32.dll","HeapCreate",0,0,api_heap_create},
    {"kernel32.dll","HeapDestroy",0,0,api_heap_destroy},
    {"kernel32.dll","InitializeCriticalSection",0,0,api_cs_initialize},
    {"kernel32.dll","InitializeCriticalSectionAndSpinCount",0,0,api_cs_initialize_spin},
    {"kernel32.dll","DeleteCriticalSection",0,0,api_cs_delete},
    {"kernel32.dll","EnterCriticalSection",0,0,api_cs_enter},
    {"kernel32.dll","LeaveCriticalSection",0,0,api_cs_leave},
    {"kernel32.dll","TryEnterCriticalSection",0,0,api_cs_try},
    {"kernel32.dll","SetCriticalSectionSpinCount",0,0,api_cs_spin},
    {"kernel32.dll","InterlockedExchange",0,0,api_atomic_exchange},
    {"kernel32.dll","InterlockedCompareExchange",0,0,api_atomic_compare},
    {"kernel32.dll","InterlockedIncrement",0,0,api_atomic_increment},
    {"kernel32.dll","InterlockedDecrement",0,0,api_atomic_decrement},
    {"kernel32.dll","InterlockedExchangeAdd",0,0,api_atomic_add},
    {"kernel32.dll","CreateEventA",0,0,api_event_create},
    {"kernel32.dll","CreateEventW",0,0,api_event_create},
    {"kernel32.dll","SetEvent",0,0,api_event_set},
    {"kernel32.dll","ResetEvent",0,0,api_event_reset},
    {"kernel32.dll","WaitForSingleObject",0,0,api_wait_single},
    {"kernel32.dll","CloseHandle",0,0,api_close_handle},
};
enum fnv_cpu_result fnv_windows_create(const struct fnv_windows_config *c,struct fnv_windows_process **out) {
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!c || !c->process_id || !c->image_bytes || c->image_bytes>c->cpu.memory_bytes ||
       c->cpu.code_high>(uint64_t)c->cpu.guest_base+c->image_bytes ||
       (c->peb_address&4095) || !inside(c,c->peb_address,4096) ||
       overlap(c->peb_address,4096,c->cpu.guest_base,c->image_bytes))return FNV_CPU_INVALID;
    struct fnv_windows_process *p=calloc(1,sizeof(*p));if(!p)return FNV_CPU_NO_MEMORY;
    p->config=*c;p->guard=(atomic_flag)ATOMIC_FLAG_INIT;
    if(c->memory) {
        unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(c->memory,&backing,&base,&size) || !inside(c,base,size) ||
           backing!=c->cpu.memory+(base-c->cpu.guest_base) ||
           pool_overlap(p,c->cpu.guest_base,c->image_bytes) || pool_overlap(p,c->peb_address,4096) ||
           !fnv_vm_retain(c->memory)){free(p);return FNV_CPU_INVALID;}
    }
    enum fnv_cpu_result result=fnv_cpu_create(&c->cpu,&p->cpu);
    if(result!=FNV_CPU_OK){fnv_vm_release_reference(c->memory);free(p);return result;}
    result=heap_prepare(p);
    if(result!=FNV_CPU_OK){(void)fnv_cpu_destroy(p->cpu);fnv_vm_release_reference(c->memory);free(p);return result;}
    result=sync_prepare(p);
    if(result!=FNV_CPU_OK){if(p->heap.blocks){write32(memory(p,c->peb_address)+0x18,0);free(p->heap.blocks);fnv_host_mutex_destroy(&p->heap_guard);}
        (void)fnv_cpu_destroy(p->cpu);fnv_vm_release_reference(c->memory);free(p);return result;}
    result=events_prepare(p);
    if(result!=FNV_CPU_OK){sync_dispose(p);if(p->heap.blocks){write32(memory(p,c->peb_address)+0x18,0);free(p->heap.blocks);fnv_host_mutex_destroy(&p->heap_guard);}
        (void)fnv_cpu_destroy(p->cpu);fnv_vm_release_reference(c->memory);free(p);return result;}
    *out=p;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_destroy(struct fnv_windows_process *p) {
    if(!p)return FNV_CPU_INVALID;
    lock(p);
    if(p->threads || p->private_heaps){unlock(p);return FNV_CPU_BUSY;}
    enum fnv_cpu_result result=fnv_cpu_destroy(p->cpu);
    unlock(p);if(result==FNV_CPU_OK){events_dispose(p);sync_dispose(p);if(p->heap.blocks){
            if(read32(memory(p,p->config.peb_address)+0x18)==p->config.heap_address)write32(memory(p,p->config.peb_address)+0x18,0);
            free(p->heap.blocks);fnv_host_mutex_destroy(&p->heap_guard);}
        fnv_vm_release_reference(p->config.memory);free(p);}return result;
}
struct fnv_cpu_runtime *fnv_windows_runtime(struct fnv_windows_process *p){return p?p->cpu:NULL;}
enum fnv_native_result fnv_windows_bind_imports(struct fnv_windows_process *p,
    struct fnv_native_image *image,struct fnv_native_import *scratch,size_t capacity,
    const struct fnv_cpu_export *extra,size_t n,size_t *count) {
    if(count)*count=0;
    if(!p || !image || image->size!=p->config.image_bytes || (n && !extra))return FNV_NATIVE_FORMAT;
    if(n>FNV_NATIVE_MAX_IMPORTS-FNV_WINDOWS_THREAD_EXPORTS)return FNV_NATIVE_CAPACITY;
    struct fnv_cpu_export *all=NULL;
    if(n){all=malloc((n+FNV_WINDOWS_THREAD_EXPORTS)*sizeof(*all));if(!all)return FNV_NATIVE_CAPACITY;
        memcpy(all,exports,sizeof(exports));memcpy(all+FNV_WINDOWS_THREAD_EXPORTS,extra,n*sizeof(*all));}
    lock(p);
    enum fnv_native_result result=fnv_cpu_bind_imports(p->cpu,image,scratch,capacity,
        all?all:exports,n+FNV_WINDOWS_THREAD_EXPORTS,count);
    unlock(p);free(all);return result;
}
enum fnv_cpu_result fnv_windows_thread_attach(struct fnv_windows_process *p,const struct fnv_windows_thread_config *c) {
    if(current.process)return FNV_CPU_BUSY;
    if(!p || !c || !c->thread_id || (c->teb_address&4095) || (c->tls_expansion_address&4095) ||
       !inside(&p->config,c->teb_address,4096) || !inside(&p->config,c->tls_expansion_address,4096) ||
       c->stack_low>=c->stack_high || !inside(&p->config,c->stack_low,c->stack_high-c->stack_low) ||
       c->state.gpr[4]<c->stack_low || (uint64_t)c->state.gpr[4]+4>c->stack_high ||
       (c->state.fs_base && c->state.fs_base!=c->teb_address) ||
       ((!c->static_tls_vector)!=(!c->static_tls_vector_bytes)) ||
       (c->static_tls_vector && (!inside(&p->config,c->static_tls_vector,c->static_tls_vector_bytes) ||
        (c->static_tls_vector&3) || (c->static_tls_vector_bytes&3))))return FNV_CPU_INVALID;
    uint32_t starts[]={c->teb_address,c->tls_expansion_address,c->stack_low};
    uint32_t sizes[]={4096,4096,c->stack_high-c->stack_low};
    if(c->static_tls_vector &&
       (overlap(c->static_tls_vector,c->static_tls_vector_bytes,p->config.cpu.guest_base,p->config.image_bytes) ||
        overlap(c->static_tls_vector,c->static_tls_vector_bytes,p->config.peb_address,4096) ||
        pool_overlap(p,c->static_tls_vector,c->static_tls_vector_bytes) || heap_overlap(p,c->static_tls_vector,c->static_tls_vector_bytes)))return FNV_CPU_INVALID;
    for(unsigned i=0;i<3;i++) {
        if(overlap(starts[i],sizes[i],p->config.cpu.guest_base,p->config.image_bytes) ||
           overlap(starts[i],sizes[i],p->config.peb_address,4096) || pool_overlap(p,starts[i],sizes[i]) || heap_overlap(p,starts[i],sizes[i]))return FNV_CPU_INVALID;
        for(unsigned j=0;j<i;j++)if(overlap(starts[i],sizes[i],starts[j],sizes[j]))return FNV_CPU_INVALID;
        if(c->static_tls_vector && overlap(starts[i],sizes[i],c->static_tls_vector,c->static_tls_vector_bytes))return FNV_CPU_INVALID;
    }
    lock(p);
    for(struct windows_thread *other=p->threads;other;other=other->next) {
        if(other->config.thread_id==c->thread_id){unlock(p);return FNV_CPU_INVALID;}
        uint32_t other_starts[]={other->config.teb_address,other->config.tls_expansion_address,other->config.stack_low};
        uint32_t other_sizes[]={4096,4096,other->config.stack_high-other->config.stack_low};
        for(unsigned i=0;i<3;i++)for(unsigned j=0;j<3;j++)
            if(overlap(starts[i],sizes[i],other_starts[j],other_sizes[j])){unlock(p);return FNV_CPU_INVALID;}
        for(unsigned i=0;i<3;i++) {
            if((c->static_tls_vector && overlap(c->static_tls_vector,c->static_tls_vector_bytes,other_starts[i],other_sizes[i])) ||
               (other->config.static_tls_vector && overlap(starts[i],sizes[i],other->config.static_tls_vector,other->config.static_tls_vector_bytes))) {
                unlock(p);return FNV_CPU_INVALID;
            }
        }
        if(c->static_tls_vector && other->config.static_tls_vector &&
           overlap(c->static_tls_vector,c->static_tls_vector_bytes,other->config.static_tls_vector,other->config.static_tls_vector_bytes)) {
            unlock(p);return FNV_CPU_INVALID;
        }
    }
    struct fnv_cpu_state state=c->state;state.fs_base=c->teb_address;
    enum fnv_cpu_result result=fnv_cpu_attach(p->cpu,&state);
    if(result!=FNV_CPU_OK){unlock(p);return result;}
    current.process=p;current.config=*c;current.teb=memory(p,c->teb_address);
    current.expansion=memory(p,c->tls_expansion_address);
    memset(current.teb,0,4096);memset(current.expansion,0,4096);
    write32(current.teb,UINT32_MAX);write32(current.teb+4,c->stack_high);write32(current.teb+8,c->stack_low);
    write32(current.teb+0x18,c->teb_address);write32(current.teb+0x20,p->config.process_id);
    write32(current.teb+0x24,c->thread_id);write32(current.teb+0x2C,c->static_tls_vector);
    write32(current.teb+0x30,p->config.peb_address);write32(current.teb+TEB_EXPANSION,c->tls_expansion_address);
    current.next=p->threads;p->threads=&current;unlock(p);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_thread_detach(void) {
    struct fnv_windows_process *p=current.process;if(!p)return FNV_CPU_NOT_ATTACHED;
    lock(p);
    if(!sync_thread_idle(p,current.config.thread_id)){unlock(p);return FNV_CPU_BUSY;}
    enum fnv_cpu_result result=fnv_cpu_detach();
    if(result==FNV_CPU_OK){
        struct windows_thread **link=&p->threads;
        while(*link!=&current)link=&(*link)->next;
        *link=current.next;memset(&current,0,sizeof(current));
    }
    unlock(p);return result;
}
