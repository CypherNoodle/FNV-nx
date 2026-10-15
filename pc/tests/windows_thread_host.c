/* Host threads exercise translated synthetic code and real Windows oracles. */
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include "windows_thread.h"
#include "fnv_sdk_runtime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define BASE 0x400000u
#define ARENA (1024u*1024u)
enum { ALLOC,FREE,GET,SET,GET_ERROR,SET_ERROR,THREAD_ID,PROCESS_ID,
    FS_CHAIN,FS_STACK_HIGH,FS_STACK_LOW,FS_SELF,FS_PID,FS_TID,FS_VECTOR,FS_PEB,FS_ERROR,
    INLINE_GET,INLINE_EXPANSION,INLINE_ERROR };
static unsigned char *arena;
static struct fnv_native_image image;
static struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
static struct fnv_windows_process *process;
static atomic_uint ready,start_gate,phase;
static uint32_t shared_index;
static void live_rejections(void);
static void yield_host(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}
static struct fnv_windows_thread_config config(unsigned id) {
    struct fnv_windows_thread_config t={0};
    t.thread_id=5000+id;t.teb_address=BASE+0x10000+id*0x20000;
    t.tls_expansion_address=t.teb_address+0x1000;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xC000;
    t.static_tls_vector=t.teb_address+0x2000;t.static_tls_vector_bytes=16;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b) {
    struct fnv_cpu_state s,got;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    s.gpr[4]=(s.fs_base+0xC000)-64;s.gpr[3]=a;s.gpr[6]=b;s.gpr[0]=0xBAD0CAFE;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    unsigned char *stack=fnv_cpu_memory(s.gpr[4],4);
    stack[0]=0;stack[1]=0x15;stack[2]=0x40;stack[3]=0;
    struct fnv_cpu_fault fault;
    enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*0x20,&fault);
    if(result!=FNV_CPU_OK)fprintf(stderr,"API %u failed %u %s\n",api,fault.kind,fault.text);
    assert(result==FNV_CPU_OK && fnv_cpu_get_state(&got)==FNV_CPU_OK);
    assert(got.gpr[4]==s.gpr[4]+4 && got.fs_base==s.fs_base);
    assert(got.gpr[3]==a && got.gpr[6]==b);return got.gpr[0];
}
static void new_process(const char *file) {
    assert(fnv_native_load_image(file,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;
    c.peb_address=BASE+0x8000;c.process_id=0x1234;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
}
static void check_layout(unsigned id) {
    struct fnv_windows_thread_config t=config(id);
    assert(call(FS_CHAIN,0,0)==UINT32_MAX && call(FS_SELF,0,0)==t.teb_address);
    assert(call(FS_STACK_HIGH,0,0)==t.stack_high && call(FS_STACK_LOW,0,0)==t.stack_low);
    assert(call(FS_PID,0,0)==0x1234 && call(PROCESS_ID,0,0)==0x1234);
    assert(call(FS_TID,0,0)==t.thread_id && call(THREAD_ID,0,0)==t.thread_id);
    assert(call(FS_VECTOR,0,0)==t.static_tls_vector && call(FS_PEB,0,0)==BASE+0x8000);
    call(INLINE_ERROR,0xDEADBEEF,0);assert(call(GET_ERROR,0,0)==0xDEADBEEF);
    call(SET_ERROR,0x87654321,0);assert(call(FS_ERROR,0,0)==0x87654321);
}
static void tls_semantics(void) {
    struct fnv_windows_thread_config t=config(0);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);check_layout(0);
    assert(fnv_windows_destroy(process)==FNV_CPU_BUSY);
    live_rejections();
    uint32_t indices[FNV_WINDOWS_TLS_SLOTS];
    for(unsigned i=0;i<FNV_WINDOWS_TLS_SLOTS;i++) {
        call(SET_ERROR,0x12345678,0);indices[i]=call(ALLOC,0,0);
        assert(indices[i]<FNV_WINDOWS_TLS_SLOTS);
        for(unsigned j=0;j<i;j++)assert(indices[j]!=indices[i]);
        assert(call(GET_ERROR,0,0)==0x12345678);
        assert(call(GET,indices[i],0)==0 && call(GET_ERROR,0,0)==0);
        call(SET_ERROR,123,0);assert(call(SET,indices[i],0xFF000000u+i)==1);
        assert(call(GET_ERROR,0,0)==123);
        uint32_t inline_value=indices[i]<64?call(INLINE_GET,indices[i],0):call(INLINE_EXPANSION,indices[i]-64,0);
        assert(inline_value==0xFF000000u+i && call(GET,indices[i],0)==inline_value);
    }
    assert(call(ALLOC,0,0)==UINT32_MAX && call(GET_ERROR,0,0)==259);
    uint32_t invalids[]={1088,UINT32_MAX,0x80000000};
    for(unsigned i=0;i<3;i++){
        assert(call(GET,invalids[i],0)==0 && call(GET_ERROR,0,0)==87);
        assert(call(SET,invalids[i],42)==0 && call(GET_ERROR,0,0)==87);
        assert(call(FREE,invalids[i],0)==0 && call(GET_ERROR,0,0)==87);
    }
    for(unsigned i=0;i<FNV_WINDOWS_TLS_SLOTS;i++) {
        call(SET_ERROR,333,0);assert(call(FREE,indices[i],0)==1 && call(GET_ERROR,0,0)==333);
        assert(call(GET,indices[i],0)==0 && call(GET_ERROR,0,0)==0);
        assert(call(FREE,indices[i],0)==0 && call(GET_ERROR,0,0)==87);
    }
    /* Get/Set minimally validate the numeric range, like Win32, not bitmap. */
    assert(call(SET,5,0x80000001)==1 && call(GET,5,0)==0x80000001);
    for(unsigned i=0;i<6;i++)assert(call(ALLOC,0,0)<FNV_WINDOWS_TLS_SLOTS);
    assert(call(GET,5,0)==0);
    shared_index=5;
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);
    assert(fnv_windows_thread_detach()==FNV_CPU_NOT_ATTACHED);
}
#ifdef _WIN32
static DWORD WINAPI worker(void *opaque) {
#else
static void *worker(void *opaque) {
#endif
    unsigned id=(unsigned)(uintptr_t)opaque;struct fnv_windows_thread_config t=config(id);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);check_layout(id);
    assert(call(GET,shared_index,0)==0);
    atomic_fetch_add(&ready,1);while(!atomic_load(&start_gate))yield_host();
    for(unsigned n=0;n<1000;n++){
        uint32_t value=0x80000000u+id*100000+n;
        call(SET_ERROR,value,0);assert(call(SET,shared_index,value)==1);
        yield_host();assert(call(GET_ERROR,0,0)==value);
        assert(call(INLINE_GET,shared_index,0)==value && call(GET,shared_index,0)==value);
        assert(call(GET_ERROR,0,0)==0);
    }
    if(id==1){atomic_store(&phase,1);while(atomic_load(&phase)!=2)yield_host();
        assert(call(FREE,shared_index,0)==1);assert(call(ALLOC,0,0)==shared_index);
        atomic_store(&phase,3);while(atomic_load(&phase)!=4)yield_host();}
    else {while(atomic_load(&phase)!=1)yield_host();assert(call(SET,shared_index,0xC0FFEE)==1);
        atomic_store(&phase,2);while(atomic_load(&phase)!=3)yield_host();
        assert(call(GET,shared_index,0)==0);atomic_store(&phase,4);}
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static void concurrent(void) {
    atomic_store(&ready,0);atomic_store(&start_gate,0);atomic_store(&phase,0);
#ifdef _WIN32
    HANDLE a=CreateThread(NULL,0,worker,(void*)(uintptr_t)1,0,NULL);
    HANDLE b=CreateThread(NULL,0,worker,(void*)(uintptr_t)2,0,NULL);assert(a && b);
#else
    pthread_t a,b;assert(!pthread_create(&a,NULL,worker,(void*)(uintptr_t)1));assert(!pthread_create(&b,NULL,worker,(void*)(uintptr_t)2));
#endif
    while(atomic_load(&ready)!=2)yield_host();atomic_store(&start_gate,1);
#ifdef _WIN32
    assert(WaitForSingleObject(a,20000)==WAIT_OBJECT_0 && WaitForSingleObject(b,20000)==WAIT_OBJECT_0);CloseHandle(a);CloseHandle(b);
#else
    assert(!pthread_join(a,NULL) && !pthread_join(b,NULL));
#endif
}
static unsigned windows_oracle(void) {
#ifdef _WIN32
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t own=call(ALLOC,0,0);DWORD oracle=TlsAlloc();assert(oracle!=TLS_OUT_OF_INDEXES);
    uint32_t values[]={0,1,0x80000000u,UINT32_MAX,0x12345678};
    for(unsigned i=0;i<5;i++){
        SetLastError(700+i);assert(TlsSetValue(oracle,(void*)(uintptr_t)values[i]));DWORD kept=GetLastError();
        call(SET_ERROR,700+i,0);assert(call(SET,own,values[i])==1 && call(GET_ERROR,0,0)==kept);
        SetLastError(900);uint32_t value=(uint32_t)(uintptr_t)TlsGetValue(oracle);DWORD error=GetLastError();
        call(SET_ERROR,900,0);assert(call(GET,own,0)==value && call(GET_ERROR,0,0)==error);
    }
    SetLastError(123);assert(!TlsGetValue(UINT32_MAX));DWORD invalid=GetLastError();
    assert(call(GET,UINT32_MAX,0)==0 && call(GET_ERROR,0,0)==invalid);
    assert(TlsFree(oracle));assert(call(FREE,own,0)==1);assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 11;
#else
    return 0;
#endif
}
static unsigned rejection_checks(void) {
    struct fnv_windows_thread_config cases[15];
    for(unsigned i=0;i<15;i++)cases[i]=config(0);
    cases[0].tls_expansion_address=cases[0].teb_address;
    cases[1].teb_address=UINT32_MAX;
    cases[2].tls_expansion_address=BASE+ARENA;
    cases[3].teb_address=BASE;cases[3].state.fs_base=BASE;
    cases[4].teb_address=BASE+0x8000;cases[4].state.fs_base=cases[4].teb_address;
    cases[5].stack_low=cases[5].stack_high;
    cases[6].state.gpr[4]=cases[6].stack_high;
    cases[7].state.fs_base+=4;
    cases[8].state.x87_top=8;
    cases[9].static_tls_vector=cases[9].teb_address+0x100;
    cases[10].static_tls_vector=cases[10].tls_expansion_address;
    cases[11].static_tls_vector=BASE+0x8000;
    cases[12].static_tls_vector_bytes=0;
    cases[13].static_tls_vector=cases[13].teb_address-4;cases[13].static_tls_vector_bytes=8;
    cases[14].static_tls_vector=0;cases[14].static_tls_vector_bytes=16;
    unsigned char *before=malloc(ARENA);assert(before);memcpy(before,arena,ARENA);
    for(unsigned i=0;i<15;i++) {
        assert(fnv_windows_thread_attach(process,&cases[i])==FNV_CPU_INVALID);
        assert(!memcmp(before,arena,ARENA));
        assert(fnv_cpu_execute(0x401500,NULL)==FNV_CPU_NOT_ATTACHED);
    }
    free(before);return 15;
}
#ifdef _WIN32
static DWORD WINAPI reject_worker(void *opaque) {
#else
static void *reject_worker(void *opaque) {
#endif
    const struct fnv_windows_thread_config *t=opaque;
    assert(fnv_windows_thread_attach(process,t)==FNV_CPU_INVALID);
    assert(fnv_cpu_execute(0x401500,NULL)==FNV_CPU_NOT_ATTACHED);
    assert(fnv_windows_thread_detach()==FNV_CPU_NOT_ATTACHED);return 0;
}
static void live_rejections(void) {
    struct fnv_windows_thread_config cases[4];
    for(unsigned i=0;i<4;i++)cases[i]=config(1);
    struct fnv_windows_thread_config parent=config(0);
    cases[0].thread_id=parent.thread_id;
    cases[1].teb_address=parent.teb_address;cases[1].state.fs_base=parent.teb_address;
    cases[2].tls_expansion_address=parent.stack_low;
    cases[3].static_tls_vector=parent.static_tls_vector;
    unsigned char *copy=malloc(ARENA);assert(copy);memcpy(copy,arena,ARENA);
    for(unsigned i=0;i<4;i++) {
#ifdef _WIN32
        HANDLE h=CreateThread(NULL,0,reject_worker,cases+i,0,NULL);assert(h);
        assert(WaitForSingleObject(h,15000)==WAIT_OBJECT_0);CloseHandle(h);
#else
        pthread_t h;assert(!pthread_create(&h,NULL,reject_worker,cases+i));assert(!pthread_join(h,NULL));
#endif
        assert(!memcmp(copy,arena,ARENA));
    }
    free(copy);
}
static void unused_export(void) { abort(); }
static void context_faults(const char *fixture) {
    for(unsigned i=0;i<2;i++) {
        new_process(fixture);size_t count=0;
        assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK);
        struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
        if(i==0){unsigned char *p=fnv_cpu_memory(t.teb_address+0xF94,4);memset(p,0,4);}
        else {struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.fs_base+=4;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);}
        struct fnv_cpu_fault fault;
        assert(fnv_cpu_execute(0x401500+GET_ERROR*0x20,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fault.kind==FNV_CPU_UNIMPLEMENTED && strstr(fault.text,i==0?"pointer":"context"));
        assert(fnv_cpu_execute(0x401500,NULL)==FNV_CPU_FAULTED);
        /* A context failure must never leave the Windows guard held. */
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    }
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);arena=calloc(1,ARENA);assert(arena);new_process(argv[1]);size_t count;
    unsigned char *original=malloc(image.size);assert(original);memcpy(original,arena,image.size);
    struct fnv_cpu_export extra={"KERNEL32.dll","GetLastError",0,0,unused_export};
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,&extra,1,&count)==FNV_NATIVE_FORMAT);
    assert(count==0 && !memcmp(original,arena,image.size));
    extra.dll="Fixture.dll";extra.name="Unused";
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,&extra,1,&count)==FNV_NATIVE_OK && count==8);
    free(original);
    unsigned rejected=rejection_checks();
    tls_semantics();concurrent();unsigned oracle=windows_oracle();assert(fnv_windows_destroy(process)==FNV_CPU_OK);
    context_faults(argv[1]);
    size_t real_imports=0;
    if(argc==3){
        free(arena);arena=calloc(1,64u*1024*1024);assert(arena);
        assert(fnv_native_load_image(argv[2],arena,64u*1024*1024,&image)==FNV_NATIVE_OK);
        struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=64u*1024*1024;
        c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=123;
        c.peb_address=(BASE+image.size+4095)&~4095u;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
        unsigned char *copy=malloc(image.size);assert(copy);memcpy(copy,arena,image.size);
        assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&real_imports)==FNV_NATIVE_UNRESOLVED);
        assert(!memcmp(copy,arena,image.size));free(copy);assert(fnv_windows_destroy(process)==FNV_CPU_OK);
    }
    free(arena);printf("{\"status\":\"PASS\",\"windows_exports\":8,\"tls_slots\":1088,\"concurrent_rounds\":2000,\"rejected_reservations\":%u,\"live_thread_conflicts_rejected\":4,\"context_faults\":2,\"oracle_comparisons\":%u,\"real_imports_checked_atomically\":%zu}\n",rejected,oracle,real_imports);return 0;
}
