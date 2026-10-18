#include <windows.h>
#include "windows_thread.h"
#include "host_wallclock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define OUTPUT (BASE+0x9004u) /* x86 ABI permits four-byte alignment. */
enum {COUNTER,FREQUENCY,DELAY,GET_ERROR,SET_ERROR,TICK,FILE_TIME,STARTUP_A,STARTUP_W};
static const unsigned counts[]={1,1,1,0,1,0,1,1,1};
static unsigned char *arena;
struct fake_clock {uint64_t value;int valid;};
static int sample(void *context,uint64_t *out){struct fake_clock *clock=context;*out=clock->value;return clock->valid;}
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
    config.startup_flags=2;assert(fnv_windows_create(&config,&process)==FNV_CPU_INVALID && !process);
    config.startup_flags=1;config.startup_show_window=12;assert(fnv_windows_create(&config,&process)==FNV_CPU_INVALID && !process);
    config.startup_show_window=SW_SHOWDEFAULT;assert(fnv_windows_create(&config,&process)==FNV_CPU_INVALID && !process);
    config.startup_show_window=7;
    struct fake_clock fake={3ull*24*3600*1000000000,1};
    if(argc==3 && strstr(argv[2],"clock")){config.performance_clock_ns=sample;config.performance_clock_context=&fake;}
    LARGE_INTEGER hz,epoch_a,epoch_b;assert(QueryPerformanceFrequency(&hz) && QueryPerformanceCounter(&epoch_a));
    assert(fnv_windows_create(&config,&process)==FNV_CPU_OK);assert(QueryPerformanceCounter(&epoch_b));struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    config.startup_flags=0;config.startup_show_window=0; /* Provider owns the snapshot. */
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==9);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_fault fault;
        if(strstr(argv[2],"clock")){
            write32(OUTPUT,0x12345678);write32(OUTPUT+4,0xabcdef01);
            if(strstr(argv[2],"backwards"))fake.value--;else fake.valid=0;
            assert(execute(COUNTER,OUTPUT,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"performance clock"));
            assert(read64()==0xabcdef0112345678ull);
        }else assert(execute(!strcmp(argv[2],"--strict-counter")?COUNTER:!strcmp(argv[2],"--strict-filetime")?FILE_TIME:!strcmp(argv[2],"--strict-startup-info")?STARTUP_A:FREQUENCY,0,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"pointer unavailable"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    LARGE_INTEGER a,b;
    uint64_t last=0;
    for(unsigned i=0;i<256;++i){call(SET_ERROR,0x5678);assert(call(FREQUENCY,OUTPUT) && read64()==10000000 && call(GET_ERROR,0)==0x5678);
        assert(QueryPerformanceCounter(&a));assert(call(COUNTER,OUTPUT));assert(QueryPerformanceCounter(&b));uint64_t now=read64();
        assert(now>=last && now>=(ns(a,hz)-ns(epoch_b,hz))/100 && now<=(ns(b,hz)-ns(epoch_a,hz))/100 && call(GET_ERROR,0)==0x5678);last=now;}
    call(COUNTER,OUTPUT);uint64_t before=read64();call(DELAY,25);call(COUNTER,OUTPUT);assert(read64()-before>=250000);call(DELAY,0);
    for(unsigned i=0;i<256;++i){
        call(SET_ERROR,0x5678);DWORD first=GetTickCount();uint32_t tick=call(TICK,0);DWORD last_tick=GetTickCount();
        assert((uint32_t)(tick-first)<=(uint32_t)(last_tick-first) && call(GET_ERROR,0)==0x5678);
        FILETIME first_time,last_time;GetSystemTimeAsFileTime(&first_time);
        write32(OUTPUT-4,0xdeadbeef);write32(OUTPUT+8,0x12345678);call(FILE_TIME,OUTPUT);uint64_t stamp=read64();
        GetSystemTimeAsFileTime(&last_time);
        uint64_t low=(uint64_t)first_time.dwLowDateTime|((uint64_t)first_time.dwHighDateTime<<32);
        uint64_t high=(uint64_t)last_time.dwLowDateTime|((uint64_t)last_time.dwHighDateTime<<32);
        assert(stamp>=low && stamp<=high && read32(OUTPUT-4)==0xdeadbeef && read32(OUTPUT+8)==0x12345678 && call(GET_ERROR,0)==0x5678);
    }
    uint64_t converted=123;
    for(unsigned api=STARTUP_A;api<=STARTUP_W;++api){
        memset(arena+OUTPUT-BASE,0xaa,72);write32(OUTPUT-4,0xdeadbeef);call(SET_ERROR,0x3456);call(api,OUTPUT);
        assert(read32(OUTPUT-4)==0xdeadbeef && read32(OUTPUT+68)==0xaaaaaaaa && call(GET_ERROR,0)==0x3456);
        for(unsigned i=0;i<68;++i){unsigned char expected=i==0?68:i==44?1:i==48?7:0;assert(arena[OUTPUT-BASE+i]==expected);}
    }
    assert(fnv_host_utc_from_unix(-11644473600ll,0,&converted) && converted==0);
    assert(fnv_host_utc_from_unix(0,999999999,&converted) && converted==116444736009999999ull);
    assert(fnv_host_utc_from_unix(-1,0,&converted) && converted==116444735990000000ull);
    assert(fnv_host_utc_from_unix(253402300799ll,999999999,&converted) && converted==2650467743999999999ull);
    converted=123;assert(!fnv_host_utc_from_unix(INT64_MAX,0,&converted) && converted==123);
    assert(!fnv_host_utc_from_unix(INT64_MIN,0,&converted) && !fnv_host_utc_from_unix(-11644473601ll,0,&converted));
    assert(!fnv_host_utc_from_unix(0,-1,&converted) && !fnv_host_utc_from_unix(0,1000000000,&converted) && !fnv_host_utc_from_unix(0,0,NULL));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    const unsigned days[]={3,7,365};
    for(unsigned i=0;i<3;++i){
        assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
        fake.value=(uint64_t)days[i]*24*3600*1000000000ull;fake.valid=1;
        config.performance_clock_ns=sample;config.performance_clock_context=&fake;
        assert(fnv_windows_create(&config,&process)==FNV_CPU_OK);
        assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK);
        assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);assert(call(COUNTER,OUTPUT) && !read64());
        call(STARTUP_A,OUTPUT);assert(read32(OUTPUT)==68 && !read32(OUTPUT+44) && !read32(OUTPUT+48));
        DWORD first_tick=GetTickCount();uint32_t real_tick=call(TICK,0);DWORD last_tick=GetTickCount();
        assert((uint32_t)(real_tick-first_tick)<=(uint32_t)(last_tick-first_tick));
        call(FILE_TIME,OUTPUT);assert(read64()>116444736000000000ull); /* QPC provider does not replace UTC/uptime. */
        fake.value+=1750;assert(call(COUNTER,OUTPUT) && read64()==17);
        fake.value+=3600000000000ull;assert(call(COUNTER,OUTPUT) && read64()==36000000017ull);
        assert(read64()*1000000/10000000==3600000001ull);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    }
    fake.valid=0;assert(fnv_windows_create(&config,&process)==FNV_CPU_INVALID && !process);
    assert(VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"counter_bracket_checks\":256,\"frequency_checks\":256,\"utc_bracket_checks\":256,\"tick_bracket_checks\":256,\"utc_epoch_and_overflow\":true,\"startup_info_32_A_W\":true,\"virtual_uptime_days\":[3,7,365],\"conversion_product_checked\":true,\"sleep_deadline_checked\":true,\"kernel_handles_required\":false,\"native_engine_started\":false}");return 0;
}
