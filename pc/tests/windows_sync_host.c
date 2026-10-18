#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define SECTION (BASE+0x9000u)
#define COUNTER (BASE+0xA000u)
#define ATOMIC (COUNTER+4)
enum {INIT,INIT_SPIN,ENTER,TRY,LEAVE,DELETE_CS,SPIN,EXCHANGE,COMPARE,INCREMENT,DECREMENT,ADD,GET_ERROR,SET_ERROR};
static unsigned char *arena;
static struct fnv_windows_process *process;
static HANDLE attempted;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c){
    const unsigned argc[]={1,2,1,1,1,1,2,2,3,1,1,2,0,1};struct fnv_cpu_state s,got;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=read32(s.fs_base+4)-256;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    uint32_t args[]={0x401500,a,b,c};for(unsigned i=0;i<4;++i)write32(s.gpr[4]+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*32,&fault);
    if(result!=FNV_CPU_OK)fprintf(stderr,"sync API %u fault %s\n",api,fault.text);
    assert(result==FNV_CPU_OK && fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[4]==s.gpr[4]+4+argc[api]*4);return got.gpr[0];
}
static struct fnv_windows_thread_config thread_config(unsigned index){
    struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xC000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static void increment_shared(void){
    call(ENTER,SECTION,0,0);assert(call(TRY,SECTION,0,0));uint32_t n=read32(COUNTER);write32(COUNTER,n+1);
    call(LEAVE,SECTION,0,0);call(LEAVE,SECTION,0,0);call(INCREMENT,ATOMIC,0,0);
}
static DWORD WINAPI worker(void *unused){
    (void)unused;struct fnv_windows_thread_config t=thread_config(1);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    call(SET_ERROR,0x9876,0,0);assert(!call(TRY,SECTION,0,0) && call(GET_ERROR,0,0,0)==0x9876);assert(SetEvent(attempted));
    call(ENTER,SECTION,0,0);call(LEAVE,SECTION,0,0);
    for(unsigned i=0;i<2000;++i)increment_shared();
    assert(call(GET_ERROR,0,0,0)==0x9876 && fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static uint64_t cpu_time(HANDLE t){
    FILETIME created,exited,kernel,user;assert(GetThreadTimes(t,&created,&exited,&kernel,&user));
    return ((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime;
}
static DWORD WINAPI fault_waiter(void *unused){
    (void)unused;struct fnv_windows_thread_config t=thread_config(1);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=t.stack_high-256;
    write32(s.gpr[4],0x401500);write32(s.gpr[4]+4,SECTION);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);assert(SetEvent(attempted));
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401540,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"peer CPU fault"));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static void strict_case(const char *mode){
    uint32_t api=ENTER,at=SECTION;
    if(strcmp(mode,"--strict-uninitialized"))call(INIT,SECTION,0,0);
    if(!strcmp(mode,"--strict-peer")){
        call(ENTER,SECTION,0,0);attempted=CreateEventW(NULL,TRUE,FALSE,NULL);assert(attempted);
        HANDLE other=CreateThread(NULL,0,fault_waiter,NULL,0,NULL);assert(other && WaitForSingleObject(attempted,5000)==WAIT_OBJECT_0);
        struct fnv_windows_sync_stats stats;ULONGLONG until=GetTickCount64()+5000;
        do{assert(fnv_windows_sync_stats(process,&stats)==FNV_CPU_OK);if(stats.waiting_threads)break;Sleep(1);}while(GetTickCount64()<until);
        assert(stats.waiting_threads==1);struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=read32(s.fs_base+4)-256;
        write32(s.gpr[4],0x401500);write32(s.gpr[4]+4,SECTION+64);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);struct fnv_cpu_fault fault;
        assert(fnv_cpu_execute(0x401540,&fault)==FNV_CPU_GUEST_FAULT && fnv_cpu_is_faulted(fnv_windows_runtime(process)));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && WaitForSingleObject(other,5000)==WAIT_OBJECT_0);DWORD code;
        assert(GetExitCodeThread(other,&code) && !code);CloseHandle(other);CloseHandle(attempted);
        assert(fnv_windows_sync_stats(process,&stats)==FNV_CPU_OK && !stats.waiting_threads);
        assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return;
    }
    if(!strcmp(mode,"--strict-nonowner"))api=LEAVE;
    else if(!strcmp(mode,"--strict-modified"))write32(SECTION+4,0);
    else if(!strcmp(mode,"--strict-delete")){call(ENTER,SECTION,0,0);api=DELETE_CS;}
    else if(!strcmp(mode,"--strict-capacity")){for(unsigned i=1;i<4;++i)call(INIT,SECTION+i*64,0,0);api=INIT;at=SECTION+256;}
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=read32(s.fs_base+4)-256;
    write32(s.gpr[4],0x401500);write32(s.gpr[4]+4,at);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*32,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    const char *expected=!strcmp(mode,"--strict-uninitialized")?"not initialized":!strcmp(mode,"--strict-nonowner")?"non-owner":
        !strcmp(mode,"--strict-modified")?"copied or modified":!strcmp(mode,"--strict-delete")?"active":"capacity/SEH";
    assert(strstr(fault.text,expected));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("strict PASS");
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.critical_section_capacity=65537;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_INVALID && !process);c.critical_section_capacity=4;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==14);
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){strict_case(argv[2]);return 0;}
    call(SET_ERROR,0x1234,0,0);assert(call(INIT_SPIN,SECTION,4000,0));
    CRITICAL_SECTION native;assert(InitializeCriticalSectionAndSpinCount(&native,4000));
    assert(call(SPIN,SECTION,8,0)==SetCriticalSectionSpinCount(&native,8));
    for(unsigned i=0;i<500;++i){
        call(ENTER,SECTION,0,0);EnterCriticalSection(&native);assert(call(TRY,SECTION,0,0)==(uint32_t)TryEnterCriticalSection(&native));
        call(ENTER,SECTION,0,0);EnterCriticalSection(&native);assert(read32(SECTION+8)==3 && read32(SECTION+12)==22);
        assert(fnv_windows_thread_detach()==FNV_CPU_BUSY);
        for(unsigned j=0;j<3;++j){call(LEAVE,SECTION,0,0);LeaveCriticalSection(&native);}
    }
    DeleteCriticalSection(&native);assert(call(GET_ERROR,0,0,0)==0x1234);
    unsigned seed=0x91af3;LONG native_atomic=0;unsigned atomic_cases=0;
    for(unsigned i=0;i<10000;++i){seed=seed*1664525u+1013904223u;uint32_t value=seed,next,expected;
        switch(i%5){
        case 0:next=call(EXCHANGE,ATOMIC,value,0);expected=(uint32_t)InterlockedExchange(&native_atomic,(LONG)value);break;
        case 1:next=call(COMPARE,ATOMIC,value,i&1?(uint32_t)native_atomic:value);expected=(uint32_t)InterlockedCompareExchange(&native_atomic,(LONG)value,i&1?native_atomic:(LONG)value);break;
        case 2:next=call(INCREMENT,ATOMIC,0,0);expected=(uint32_t)InterlockedIncrement(&native_atomic);break;
        case 3:next=call(DECREMENT,ATOMIC,0,0);expected=(uint32_t)InterlockedDecrement(&native_atomic);break;
        default:next=call(ADD,ATOMIC,value,0);expected=(uint32_t)InterlockedExchangeAdd(&native_atomic,(LONG)value);break;
        }
        assert(next==expected && read32(ATOMIC)==(uint32_t)native_atomic);atomic_cases++;
    }
    write32(ATOMIC,UINT32_MAX);assert(call(INCREMENT,ATOMIC,0,0)==0 && call(DECREMENT,ATOMIC,0,0)==UINT32_MAX);write32(ATOMIC,0);
    call(ENTER,SECTION,0,0);attempted=CreateEventW(NULL,TRUE,FALSE,NULL);assert(attempted);HANDLE other=CreateThread(NULL,0,worker,NULL,0,NULL);assert(other);
    assert(WaitForSingleObject(attempted,5000)==WAIT_OBJECT_0);struct fnv_windows_sync_stats stats;
    ULONGLONG deadline=GetTickCount64()+5000;
    do{assert(fnv_windows_sync_stats(process,&stats)==FNV_CPU_OK);if(stats.waiting_threads)break;Sleep(1);}while(GetTickCount64()<deadline);
    assert(stats.waiting_threads==1 && stats.owned_sections==1);uint64_t before=cpu_time(other);Sleep(150);uint64_t waiting_cpu=cpu_time(other)-before;
    assert(waiting_cpu<500000 && WaitForSingleObject(other,0)==WAIT_TIMEOUT);call(LEAVE,SECTION,0,0);
    for(unsigned i=0;i<2000;++i)increment_shared();
    assert(WaitForSingleObject(other,10000)==WAIT_OBJECT_0);DWORD code;assert(GetExitCodeThread(other,&code) && !code);CloseHandle(other);CloseHandle(attempted);
    assert(read32(COUNTER)==4000 && read32(ATOMIC)==4000 && call(GET_ERROR,0,0,0)==0x1234);
    for(unsigned i=1;i<4;++i)assert(call(INIT_SPIN,SECTION+i*64,0,0));
    assert(!call(INIT_SPIN,SECTION+256,0,0) && call(GET_ERROR,0,0,0)==8);
    for(unsigned i=1;i<4;++i)call(DELETE_CS,SECTION+i*64,0,0);
    call(DELETE_CS,SECTION,0,0);call(INIT,SECTION,0,0);call(DELETE_CS,SECTION,0,0);
    assert(fnv_windows_sync_stats(process,&stats)==FNV_CPU_OK && !stats.live_sections && !stats.owned_sections && !stats.waiting_threads && stats.blocking_waits && stats.wakeups && stats.try_failures);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"critical_section_sequences\":500,\"atomic_comparisons\":%u,\"concurrent_increments\":4000,\"blocked_thread_cpu_100ns\":%llu,\"blocking_waits\":%llu,\"iat_imports\":14,\"all_sections_released\":true,\"native_engine_started\":false}\n",atomic_cases,(unsigned long long)waiting_cpu,(unsigned long long)stats.blocking_waits);return 0;
}
