#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
enum {CREATE_EVENT,SET_EVENT,RESET_EVENT,WAIT_ONE,CLOSE_HANDLE,CREATE_W,GET_ERROR,SET_ERROR
#ifdef EXTENDED_SYNC_TESTS
    ,CREATE_SEM,RELEASE_SEM,CREATE_MUTEX,RELEASE_MUTEX,WAIT_MANY,CREATE_SEM_W,CREATE_MUTEX_W
#endif
};
static const unsigned args_count[]={4,1,1,2,1,4,0,1
#ifdef EXTENDED_SYNC_TESTS
    ,4,3,3,1,4,4,3
#endif
};
static unsigned char *arena;
static struct fnv_windows_process *process;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static enum fnv_cpu_result execute(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=read32(s.fs_base+4)-256;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    uint32_t args[]={0x401500,a,b,c,d};for(unsigned i=0;i<5;++i)write32(s.gpr[4]+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){struct fnv_cpu_state got;assert(fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[4]==s.gpr[4]+4+args_count[api]*4);}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=execute(api,a,b,c,d,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"event API %u fault %s\n",api,fault.text);
    struct fnv_cpu_state s;assert(r==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK);return s.gpr[0];
}
static struct fnv_windows_thread_config thread_config(unsigned index){
    struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xC000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static struct fnv_windows_handle_stats stats(void){struct fnv_windows_handle_stats s;assert(fnv_windows_handle_stats(process,&s)==FNV_CPU_OK);return s;}
static void wait_count(unsigned n){
    ULONGLONG limit=GetTickCount64()+5000;
    while(stats().waiting_threads!=n && GetTickCount64()<limit)Sleep(1);
    assert(stats().waiting_threads==n);
}
struct group {uint32_t event;HANDLE gates[2],done;unsigned rounds,peer,closed;};
struct work {struct group *group;unsigned index;};
static DWORD WINAPI waiter(void *context){
    struct work *w=context;struct group *g=w->group;struct fnv_windows_thread_config t=thread_config(w->index+1);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);call(SET_ERROR,0x9832+w->index,0,0,0);
    for(unsigned i=0;i<g->rounds;++i){assert(WaitForSingleObject(g->gates[w->index],5000)==WAIT_OBJECT_0);
        if(g->peer){struct fnv_cpu_fault fault;assert(execute(WAIT_ONE,g->event,UINT32_MAX,0,0,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"peer CPU fault"));}
        else {uint32_t r=call(WAIT_ONE,g->event,UINT32_MAX,0,0);
            assert(r==(g->closed?WAIT_FAILED:WAIT_OBJECT_0));assert(call(GET_ERROR,0,0,0,0)==(g->closed?ERROR_INVALID_HANDLE:0x9832+w->index));}
        assert(ReleaseSemaphore(g->done,1,NULL));
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static void finish(HANDLE *threads,struct group *g,unsigned count){
    for(unsigned i=0;i<count;++i){assert(WaitForSingleObject(threads[i],5000)==WAIT_OBJECT_0);DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);
        CloseHandle(threads[i]);CloseHandle(g->gates[i]);}CloseHandle(g->done);
}
static uint64_t cpu_time(HANDLE t){FILETIME c,e,k,u;assert(GetThreadTimes(t,&c,&e,&k,&u));return ((uint64_t)k.dwHighDateTime<<32)+k.dwLowDateTime+((uint64_t)u.dwHighDateTime<<32)+u.dwLowDateTime;}
static uint64_t contention(int manual){
    struct group g={0};g.event=call(CREATE_EVENT,0,(uint32_t)manual,0,0);g.rounds=150;assert(g.event);
    g.done=CreateSemaphoreW(NULL,0,2,NULL);assert(g.done);HANDLE threads[2];struct work w[2];
    for(unsigned i=0;i<2;++i){g.gates[i]=CreateEventW(NULL,FALSE,FALSE,NULL);w[i]=(struct work){&g,i};threads[i]=CreateThread(NULL,0,waiter,w+i,0,NULL);assert(g.gates[i] && threads[i]);}
    uint64_t waiting_cpu=0;
    for(unsigned n=0;n<g.rounds;++n){assert(SetEvent(g.gates[0]) && SetEvent(g.gates[1]));wait_count(2);
        if(!n){uint64_t before=cpu_time(threads[0])+cpu_time(threads[1]);Sleep(150);waiting_cpu=cpu_time(threads[0])+cpu_time(threads[1])-before;assert(waiting_cpu<500000);}
        assert(call(SET_EVENT,g.event,0,0,0) && call(RESET_EVENT,g.event,0,0,0));
        assert(WaitForSingleObject(g.done,5000)==WAIT_OBJECT_0);
        if(!manual){assert(WaitForSingleObject(g.done,0)==WAIT_TIMEOUT);wait_count(1);assert(call(SET_EVENT,g.event,0,0,0) && call(RESET_EVENT,g.event,0,0,0));}
        assert(WaitForSingleObject(g.done,5000)==WAIT_OBJECT_0);wait_count(0);
        assert(call(WAIT_ONE,g.event,0,0,0)==WAIT_TIMEOUT);
    }
    finish(threads,&g,2);assert(call(CLOSE_HANDLE,g.event,0,0,0));return waiting_cpu;
}
static void close_pending(void){
    struct group g={0};g.event=call(CREATE_EVENT,0,0,0,0);g.rounds=1;g.closed=1;g.done=CreateSemaphoreW(NULL,0,1,NULL);
    g.gates[0]=CreateEventW(NULL,FALSE,TRUE,NULL);struct work w={&g,0};HANDLE t=CreateThread(NULL,0,waiter,&w,0,NULL);assert(t);wait_count(1);
    assert(call(CLOSE_HANDLE,g.event,0,0,0));uint32_t next=call(CREATE_EVENT,0,0,0,0);assert(next && next!=g.event);
    assert(WaitForSingleObject(g.done,5000)==WAIT_OBJECT_0);finish(&t,&g,1);assert(call(CLOSE_HANDLE,next,0,0,0));
}
static void strict_case(const char *mode){
    if(!strcmp(mode,"--strict-peer")){
        struct group g={0};g.event=call(CREATE_EVENT,0,0,0,0);g.rounds=1;g.peer=1;g.done=CreateSemaphoreW(NULL,0,1,NULL);g.gates[0]=CreateEventW(NULL,FALSE,TRUE,NULL);
        struct work w={&g,0};HANDLE t=CreateThread(NULL,0,waiter,&w,0,NULL);assert(t);wait_count(1);struct fnv_cpu_fault fault;
        assert(execute(CREATE_EVENT,0,0,0,BASE+0x9000,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && WaitForSingleObject(g.done,5000)==WAIT_OBJECT_0);finish(&t,&g,1);assert(!stats().waiting_threads);
    }else {struct fnv_cpu_fault fault;uint32_t name=!strcmp(mode,"--strict-name")?BASE+0x9000:0,attr=!strcmp(mode,"--strict-security")?BASE+0x9000:0;
        assert(execute(CREATE_EVENT,attr,0,0,name,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED && strstr(fault.text,!strcmp(mode,"--strict-disabled")?"provider unavailable":"named/security"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK);}
    assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.kernel_handle_capacity=65537;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_INVALID && !process);c.kernel_handle_capacity=argc==3 && !strcmp(argv[2],"--strict-disabled")?0:4;
    if(argc==2){
        const uint32_t capacities[]={1,3,1024,65536};
        for(unsigned i=0;i<4;++i){c.kernel_handle_capacity=capacities[i];
            uint32_t bits=0;while((1u<<bits)<capacities[i])bits++;uint32_t maximum=(1u<<(26-bits))-1;
            c.kernel_generation_limit=maximum+1;assert(fnv_windows_create(&c,&process)==FNV_CPU_INVALID && !process);
            c.kernel_generation_limit=0;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
            struct fnv_windows_handle_stats budget=stats();assert(!budget.total_creations && !budget.retired_slots && budget.generation_limit==maximum);
            assert(budget.remaining_creations==(uint64_t)maximum*capacities[i]);
            assert(fnv_windows_destroy(process)==FNV_CPU_OK);
        }
        c.kernel_handle_capacity=4;
    }
    c.kernel_generation_limit=c.kernel_handle_capacity?4095:0;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==sizeof(args_count)/sizeof(*args_count));
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);if(argc==3){strict_case(argv[2]);return 0;}
    unsigned comparisons=0,seed=0x59ab83;
    for(unsigned trial=0;trial<400;++trial){unsigned manual=trial&1,initial=(trial>>1)&1;
        call(SET_ERROR,0x1234,0,0,0);SetLastError(0x1234);HANDLE native=CreateEventA(NULL,(BOOL)manual,(BOOL)initial,NULL);DWORD err=GetLastError();
        uint32_t h=call(CREATE_EVENT,0,manual,initial,0);assert(h && !(h&3) && native && call(GET_ERROR,0,0,0,0)==err);
        for(unsigned i=0;i<30;++i){seed=seed*1664525u+1013904223u;unsigned op=seed%3;call(SET_ERROR,0x3579,0,0,0);SetLastError(0x3579);
            uint32_t result,expected;if(!op){result=call(SET_EVENT,h,0,0,0);expected=SetEvent(native)!=0;}
            else if(op==1){result=call(RESET_EVENT,h,0,0,0);expected=ResetEvent(native)!=0;}
            else {result=call(WAIT_ONE,h,0,0,0);expected=WaitForSingleObject(native,0);}
            assert(result==expected && call(GET_ERROR,0,0,0,0)==GetLastError());comparisons++;}
        assert(call(CLOSE_HANDLE,h,0,0,0) && CloseHandle(native));
        assert(!call(SET_EVENT,h,0,0,0) && call(GET_ERROR,0,0,0,0)==6);
    }
    uint32_t h=call(CREATE_W,0,0,1,0);assert(h && call(WAIT_ONE,h,0,0,0)==WAIT_OBJECT_0 && call(WAIT_ONE,h,0,0,0)==WAIT_TIMEOUT);
    LARGE_INTEGER before,after,frequency;assert(QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&before));
    assert(call(WAIT_ONE,h,25,0,0)==WAIT_TIMEOUT && QueryPerformanceCounter(&after));
    assert((after.QuadPart-before.QuadPart)*1000>=frequency.QuadPart*25);
    assert(call(CLOSE_HANDLE,h,0,0,0) && !call(CLOSE_HANDLE,h,0,0,0));
    assert(call(WAIT_ONE,h,0,0,0)==WAIT_FAILED && call(GET_ERROR,0,0,0,0)==6);
    uint32_t objects[4];for(unsigned i=0;i<4;++i){objects[i]=call(CREATE_EVENT,0,0,0,0);assert(objects[i]);}
    assert(!call(CREATE_EVENT,0,0,0,0) && call(GET_ERROR,0,0,0,0)==8);for(unsigned i=0;i<4;++i)assert(call(CLOSE_HANDLE,objects[i],0,0,0));
    uint64_t manual_cpu=contention(1),auto_cpu=contention(0);close_pending();
    unsigned recycled=0;while((h=call(CREATE_EVENT,0,0,0,0))){assert(call(CLOSE_HANDLE,h,0,0,0));recycled++;assert(recycled<=16380);}
    assert(recycled>10000 && call(GET_ERROR,0,0,0,0)==8);struct fnv_windows_handle_stats s=stats();assert(!s.live_handles && !s.waiting_threads && s.blocking_waits && s.timeouts && s.retired_slots==4 && s.generation_limit==4095 && !s.remaining_creations && s.total_creations==16380);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"event_oracle_comparisons\":%u,\"set_reset_contention_rounds\":300,\"manual_blocked_cpu_100ns\":%llu,\"auto_blocked_cpu_100ns\":%llu,\"generation_recycles\":%u,\"all_handles_released\":true,\"native_engine_started\":false}\n",comparisons,(unsigned long long)manual_cpu,(unsigned long long)auto_cpu,recycled);return 0;
}
