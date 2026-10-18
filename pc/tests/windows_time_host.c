#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define OUTPUT (BASE+0x9004u) /* x86 ABI permits four-byte alignment. */
enum {COUNTER,FREQUENCY,DELAY,GET_ERROR,SET_ERROR};
static const unsigned counts[]={1,1,1,0,1};
static unsigned char *arena;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(v>>(i*8));}
static uint64_t read64(void){return read32(OUTPUT)|((uint64_t)read32(OUTPUT+4)<<32);}
static enum fnv_cpu_result execute(unsigned api,uint32_t arg,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);
    write32(state.gpr[4],0x401500);write32(state.gpr[4]+4,arg);enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*32,fault);
    if(result==FNV_CPU_OK)assert(fnv_cpu_get_state(&state)==FNV_CPU_OK && state.gpr[4]==BASE+0x1bf04+counts[api]*4);
    return result;
}
static uint32_t call(unsigned api,uint32_t arg){struct fnv_cpu_fault fault;assert(execute(api,arg,&fault)==FNV_CPU_OK);struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);return state.gpr[0];}
static uint64_t ns(LARGE_INTEGER ticks,LARGE_INTEGER frequency){uint64_t n=(uint64_t)ticks.QuadPart,hz=(uint64_t)frequency.QuadPart;return n/hz*1000000000+(n%hz)*1000000000/hz;}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config config={0};config.cpu.memory=arena;config.cpu.guest_base=BASE;config.cpu.memory_bytes=ARENA;config.cpu.code_low=BASE+0x1000;config.cpu.code_high=BASE+image.size;
    config.image_bytes=image.size;config.peb_address=BASE+0x8000;config.process_id=11;struct fnv_windows_process *process;
    assert(fnv_windows_create(&config,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==5);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_fault fault;assert(execute(!strcmp(argv[2],"--strict-counter")?COUNTER:FREQUENCY,0,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"pointer unavailable"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    LARGE_INTEGER hz,a,b;assert(QueryPerformanceFrequency(&hz) && hz.QuadPart>0 && hz.QuadPart<=UINT32_MAX);
    uint64_t last=0;
    for(unsigned i=0;i<256;++i){call(SET_ERROR,0x5678);assert(call(FREQUENCY,OUTPUT) && read64()==1000000000 && call(GET_ERROR,0)==0x5678);
        assert(QueryPerformanceCounter(&a));assert(call(COUNTER,OUTPUT));assert(QueryPerformanceCounter(&b));uint64_t now=read64();
        assert(now>=last && now>=ns(a,hz) && now<=ns(b,hz) && call(GET_ERROR,0)==0x5678);last=now;}
    call(COUNTER,OUTPUT);uint64_t before=read64();call(DELAY,25);call(COUNTER,OUTPUT);assert(read64()-before>=25000000);call(DELAY,0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"counter_bracket_checks\":256,\"frequency_checks\":256,\"sleep_deadline_checked\":true,\"kernel_handles_required\":false,\"native_engine_started\":false}");return 0;
}
