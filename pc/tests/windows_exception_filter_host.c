#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
enum {FILTER,GET_ERROR,SET_ERROR};
static unsigned char *arena;
static void put32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(i*8));}
static uint32_t call(unsigned api,uint32_t arg){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0x401500);put32(s.gpr[4]+4,arg);
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*32,&fault)==FNV_CPU_OK);
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+(api==GET_ERROR?0:4));return s.gpr[0];
}
static LONG WINAPI first(EXCEPTION_POINTERS *p){(void)p;return EXCEPTION_CONTINUE_SEARCH;}
static LONG WINAPI second(EXCEPTION_POINTERS *p){(void)p;return EXCEPTION_CONTINUE_SEARCH;}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    LPTOP_LEVEL_EXCEPTION_FILTER callbacks[]={NULL,first,second},saved=SetUnhandledExceptionFilter(NULL);
    const uint32_t tokens[]={0,0x401800,0x401820};unsigned previous=0;uint32_t rng=0x23456;
    for(unsigned i=0;i<5000;i++){
        rng=rng*1664525+1013904223;unsigned index=rng%3;
        call(SET_ERROR,0x3456);SetLastError(0x3456);LPTOP_LEVEL_EXCEPTION_FILTER observed=SetUnhandledExceptionFilter(callbacks[index]);DWORD error=GetLastError();
        assert(observed==callbacks[previous] && call(FILTER,tokens[index])==tokens[previous] && call(GET_ERROR,0)==error);previous=index;
    }
    assert(SetUnhandledExceptionFilter(NULL)==callbacks[previous]);SetUnhandledExceptionFilter(saved);
    assert(call(FILTER,0)==tokens[previous] && !call(FILTER,0));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    /* State belongs to the virtual process, not to the native global handler. */
    assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK && fnv_windows_create(&c,&process)==FNV_CPU_OK);
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(!call(FILTER,0x401800) && call(FILTER,0)==0x401800);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"windows_registration_comparisons\":5000,\"process_reset\":true,\"filter_dispatch_tested\":false,\"native_engine_started\":false}");return 0;
}
