/* Synthetic imports and generated routines only; never launches the game. */
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include "cpu_runtime.h"
#include "fnv_sdk_runtime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define BASE 0x400000u
#define ARENA (1024u*1024u)
#define SUM 0x401500u
#define HALT 0x401580u
#define ICALL 0x401600u
#define CPUID 0x401680u
#define TICKS 0x401700u
#define ORDINAL 0x401780u
#define READ_FS 0x401800u
#define ITAIL 0x401880u
#define MARKER 0x401C00u
static unsigned char *arena;
static const char *fixture;
static struct fnv_native_image image;
static struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
static struct fnv_cpu_runtime *runtime;
static atomic_uint ready,start_gate;
static atomic_uint peer_entered,peer_release;
static atomic_uint_fast64_t clock_value;
static unsigned ordinal_mode,diagnostics;
static void yield_host(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}
static void put32(uint32_t at,uint32_t value) {
    assert(at>=BASE && (uint64_t)at+4<=BASE+ARENA);
    for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(8*i));
}
static uint32_t read32(uint32_t at) {
    const unsigned char *p=arena+at-BASE;
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void native_sum(void) {
    uint32_t a=fnv_cpu_argument_u32(0);
    yield_host(); /* Force overlapping calls across real host threads. */
    uint32_t b=fnv_cpu_argument_u32(1);
    fnv_cpu_return_u32(a+b,8);
}
static void native_ordinal(void) {
    if(ordinal_mode==1)assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_BUSY);
    if(ordinal_mode==2)(void)fnv_cpu_memory(UINT32_MAX,4);
    if(ordinal_mode==3)(void)fnv_cpu_argument_u32(UINT_MAX);
    if(ordinal_mode==4){
        atomic_store(&peer_entered,1);
        while(!atomic_load(&peer_release))yield_host();
    }
    fnv_cpu_return_u32(42,0);
}
static int cpu_provider(void *ctx,uint32_t leaf,uint32_t subleaf,uint32_t out[4]) {
    (void)ctx;out[0]=leaf+10;out[1]=subleaf+20;out[2]=leaf^subleaf;out[3]=0x12345678;return 1;
}
static int clock_provider(void *ctx,uint64_t *ticks) {
    (void)ctx;*ticks=atomic_fetch_add(&clock_value,1);return 1;
}
static void diagnostic(void *ctx,const struct fnv_cpu_fault *fault) {
    (void)ctx;assert(fault->kind && fault->text[0]);diagnostics++;
    assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_BUSY);
    assert(fnv_cpu_detach()==FNV_CPU_BUSY);
}
static struct fnv_cpu_export exports[]={
    {"Fixture.DLL","Sum",0,0,native_sum},
    {"fixture.dll",NULL,1,1,native_ordinal}
};
static void new_session(int cpu,int clock) {
    assert(fnv_native_load_image(fixture,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config c={0};c.memory=arena;c.guest_base=BASE;c.memory_bytes=ARENA;
    c.code_low=SUM;c.code_high=0x401A00;c.diagnostic=diagnostic;
    c.cpuid=cpu?cpu_provider:NULL;c.timestamp=clock?clock_provider:NULL;c.timestamp_hz=clock?1000000:0;
    assert(fnv_cpu_create(&c,&runtime)==FNV_CPU_OK);
}
static void bind_all(void) {
    size_t count=0;
    assert(fnv_cpu_bind_imports(runtime,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,2,&count)==FNV_NATIVE_OK);
    assert(count==2 && read32(BASE+imports[0].iat_rva)==0xFE000000u && read32(BASE+imports[1].iat_rva)==0xFE000004u);
}
static struct fnv_cpu_state initial(unsigned id) {
    struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-0x1000-id*0x2000,BASE+ARENA-0x8000-id*0x1000);
    for(unsigned i=0;i<8;i++){
        if(i!=4)s.gpr[i]=0x12340000u+1000*id+i;
        s.x87[i]=id*1000+i+0.5;s.mmx[i]=UINT64_C(0x0102030405060708)^id^i;
        for(unsigned j=0;j<16;j++)s.xmm[i][j]=(unsigned char)(100*id+16*i+j);
    }
    s.direction=(int32_t)(id&1);s.fp_compare=(int32_t)id;s.fp_status=0x4000;s.seh_ebp=s.gpr[5];
    put32(s.gpr[4],0xFFFFFFF0u);put32(s.fs_base+0x18,s.fs_base);return s;
}
static void close_session(void) { assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK); }

#ifdef _WIN32
static DWORD WINAPI worker(void *arg) {
#else
static void *worker(void *arg) {
#endif
    unsigned id=(unsigned)(uintptr_t)arg;struct fnv_cpu_state s=initial(id),got;
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);atomic_fetch_add(&ready,1);
    while(!atomic_load(&start_gate))yield_host();
    for(unsigned n=0;n<500;n++){
        s.gpr[3]=id*100000+n;s.gpr[6]=17+id;
        assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
        assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_OK);assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);
        assert(got.gpr[0]==s.gpr[3]+s.gpr[6] && got.gpr[4]==s.gpr[4]+4 && got.gpr[3]==s.gpr[3]);
        assert(got.fs_base==s.fs_base && got.direction==s.direction && got.fp_control==s.fp_control);
        assert(!memcmp(got.xmm,s.xmm,sizeof(s.xmm)) && !memcmp(got.mmx,s.mmx,sizeof(s.mmx)) && !memcmp(got.x87,s.x87,sizeof(s.x87)));
    }
    assert(fnv_cpu_icall_count==1000 && fnv_cpu_icall_trace_idx==1000);
    for(unsigned i=0;i<16;i++)assert(fnv_cpu_icall_trace[i]==((i&1)?0xFE000000u:SUM));
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);assert(fnv_cpu_execute(READ_FS,NULL)==FNV_CPU_OK);
    assert(fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[0]==s.fs_base);
    assert(fnv_cpu_detach()==FNV_CPU_OK);return 0;
}
static void concurrent(void) {
    new_session(1,1);bind_all();atomic_store(&ready,0);atomic_store(&start_gate,0);
#ifdef _WIN32
    HANDLE a=CreateThread(NULL,0,worker,(void*)(uintptr_t)1,0,NULL),b=CreateThread(NULL,0,worker,(void*)(uintptr_t)2,0,NULL);
    assert(a && b);
#else
    pthread_t a,b;assert(!pthread_create(&a,NULL,worker,(void*)(uintptr_t)1));assert(!pthread_create(&b,NULL,worker,(void*)(uintptr_t)2));
#endif
    while(atomic_load(&ready)!=2)yield_host();
    atomic_store(&start_gate,1);
#ifdef _WIN32
    assert(WaitForSingleObject(a,15000)==WAIT_OBJECT_0 && WaitForSingleObject(b,15000)==WAIT_OBJECT_0);CloseHandle(a);CloseHandle(b);
#else
    assert(!pthread_join(a,NULL) && !pthread_join(b,NULL));
#endif
    assert(!diagnostics && fnv_cpu_destroy(runtime)==FNV_CPU_OK);
}
static void binding_checks(void) {
    assert(!fnv_cpu_import_token(native_sum) && !fnv_cpu_import_token(NULL));
    new_session(0,0);unsigned char *before=malloc(image.size);assert(before);memcpy(before,image.data,image.size);
    size_t count=0;
    assert(fnv_cpu_bind_imports(runtime,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,1,&count)==FNV_NATIVE_UNRESOLVED);
    assert(!memcmp(before,image.data,image.size));
    struct fnv_cpu_state s=initial(0);assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);
    assert(!fnv_cpu_import_token(native_sum));
    assert(!recomp_lookup_kernel(0xFE000000u) && fnv_cpu_destroy(runtime)==FNV_CPU_BUSY);assert(fnv_cpu_detach()==FNV_CPU_OK);
    struct fnv_cpu_export duplicate[]={exports[0],exports[0]};duplicate[1].dll="FIXTURE.dll";
    assert(fnv_cpu_bind_imports(runtime,&image,imports,FNV_NATIVE_MAX_IMPORTS,duplicate,2,&count)==FNV_NATIVE_FORMAT);
    assert(!memcmp(before,image.data,image.size));bind_all();
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);assert(recomp_lookup_kernel(0xFE000000u)==native_sum);
    assert(fnv_cpu_import_token(native_sum)==0xFE000000u && fnv_cpu_import_token(native_ordinal)==0xFE000004u);
    assert(!fnv_cpu_import_token(NULL));
    assert(!recomp_lookup_kernel(0xFE000001u) && !recomp_lookup_kernel(0xFE001000u));
    ordinal_mode=1;assert(fnv_cpu_execute(ORDINAL,NULL)==FNV_CPU_OK);struct fnv_cpu_state got;
    assert(fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[0]==42 && got.gpr[4]==s.gpr[4]+4);
    close_session();assert(!fnv_cpu_import_token(native_sum));free(before);ordinal_mode=0;
}
static void fault_case(uint32_t entry,uint32_t target,unsigned mode,enum fnv_cpu_fault_kind kind) {
    new_session(0,0);bind_all();ordinal_mode=mode;
    struct fnv_cpu_state s=initial(0);s.gpr[0]=target;put32(MARKER,0);
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);struct fnv_cpu_fault fault;
    assert(fnv_cpu_execute(entry,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==kind && fault.entry==entry);
    assert(!read32(MARKER) && fault.call_count && fault.trace[0]==entry);
    if(entry==HALT)assert(fault.state.gpr[0]==0x12345678u);
    if(entry==ICALL)assert(fault.state.gpr[0]==target && fault.state.gpr[4]==s.gpr[4]-4);
    if(entry==ITAIL)assert(fault.state.gpr[0]==target && fault.state.gpr[4]==s.gpr[4]);
    assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_FAULTED && fnv_cpu_set_state(&s)==FNV_CPU_FAULTED);
    close_session();ordinal_mode=0;
}
static void cpu_clock(void) {
    new_session(1,1);bind_all();struct fnv_cpu_state s=initial(0),got;struct fnv_cpu_fault f;
    s.gpr[0]=7;s.gpr[1]=3;assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);
    assert(fnv_cpu_execute(CPUID,NULL)==FNV_CPU_OK);assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);
    assert(got.gpr[0]==17 && got.gpr[3]==23 && got.gpr[1]==4 && got.gpr[2]==0x12345678);
    atomic_store(&clock_value,UINT64_C(0x100000005));assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    assert(fnv_cpu_execute(TICKS,NULL)==FNV_CPU_OK);assert(fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[0]==5 && got.gpr[2]==1);
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);atomic_store(&clock_value,1);
    assert(fnv_cpu_execute(TICKS,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_CLOCK_BACKWARDS);close_session();
}
#ifdef _WIN32
static DWORD WINAPI peer_worker(void *unused) {
#else
static void *peer_worker(void *unused) {
#endif
    (void)unused;struct fnv_cpu_state s=initial(1);struct fnv_cpu_fault fault;
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);
    assert(fnv_cpu_execute(ORDINAL,&fault)==FNV_CPU_GUEST_FAULT);
    assert(fault.kind==FNV_CPU_PEER_FAULT && fault.entry==ORDINAL);
    assert(fault.state.gpr[0]==42 && fault.state.gpr[4]==s.gpr[4]+4);
    assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_FAULTED);
    assert(fnv_cpu_set_state(&s)==FNV_CPU_FAULTED);
    assert(fnv_cpu_detach()==FNV_CPU_OK);return 0;
}
static void peer_fault(void) {
    new_session(0,0);bind_all();ordinal_mode=4;
    atomic_store(&peer_entered,0);atomic_store(&peer_release,0);
#ifdef _WIN32
    HANDLE peer=CreateThread(NULL,0,peer_worker,NULL,0,NULL);assert(peer);
#else
    pthread_t peer;assert(!pthread_create(&peer,NULL,peer_worker,NULL));
#endif
    while(!atomic_load(&peer_entered))yield_host();
    struct fnv_cpu_state s=initial(0);struct fnv_cpu_fault fault;
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);
    assert(fnv_cpu_execute(HALT,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    assert(fnv_cpu_detach()==FNV_CPU_OK);
    assert(fnv_cpu_destroy(runtime)==FNV_CPU_BUSY);
    assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_FAULTED);
    atomic_store(&peer_release,1);
#ifdef _WIN32
    assert(WaitForSingleObject(peer,15000)==WAIT_OBJECT_0);CloseHandle(peer);
#else
    assert(!pthread_join(peer,NULL));
#endif
    assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK);ordinal_mode=0;
}
static void fatal_signal(int signal_value) { (void)signal_value;_Exit(77); }
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--fatal")){signal(SIGABRT,fatal_signal);recomp_icall_fail_log(0x401888);return 9;}
    assert(argc==2 || argc==3);fixture=argv[1];arena=calloc(1,ARENA);assert(arena);
    assert(fnv_cpu_execute(SUM,NULL)==FNV_CPU_NOT_ATTACHED);binding_checks();concurrent();
    fault_case(HALT,0,0,FNV_CPU_UNIMPLEMENTED);
    fault_case(ICALL,0x4019F0,0,FNV_CPU_UNKNOWN_TARGET);fault_case(ICALL,0x1337,0,FNV_CPU_NOT_CODE);
    fault_case(ICALL,0xFE000100u,0,FNV_CPU_UNKNOWN_TARGET);fault_case(ITAIL,0x4019F0,0,FNV_CPU_UNKNOWN_TARGET);
    fault_case(ORDINAL,0,2,FNV_CPU_BAD_MEMORY);fault_case(ORDINAL,0,3,FNV_CPU_BAD_MEMORY);
    fault_case(CPUID,7,0,FNV_CPU_CPUID_UNAVAILABLE);fault_case(TICKS,0,0,FNV_CPU_CLOCK_UNAVAILABLE);cpu_clock();peer_fault();
    size_t real_imports=0;
    if(argc==3){
        free(arena);arena=calloc(1,64u*1024*1024);assert(arena);
        assert(fnv_native_load_image(argv[2],arena,64u*1024*1024,&image)==FNV_NATIVE_OK);
        struct fnv_cpu_config c={0};c.memory=arena;c.guest_base=image.base;c.memory_bytes=64u*1024*1024;
        c.code_low=image.base+0x1000;c.code_high=image.base+image.size;
        assert(fnv_cpu_create(&c,&runtime)==FNV_CPU_OK);unsigned char *before=malloc(image.size);assert(before);memcpy(before,image.data,image.size);
        assert(fnv_cpu_bind_imports(runtime,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&real_imports)==FNV_NATIVE_UNRESOLVED);
        assert(!memcmp(before,image.data,image.size));assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK);free(before);
    }
    free(arena);printf("{\"status\":\"PASS\",\"concurrent_calls\":1000,\"guest_fault_cases\":12,\"peer_fault_invalidates_session\":true,\"diagnostics\":%u,\"real_imports_rejected_atomically\":%zu}\n",diagnostics,real_imports);return 0;
}
