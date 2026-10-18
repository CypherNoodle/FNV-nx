/* Own harness: execute only one explicitly selected translated initializer. */
#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#include "cookie_profile.h"
#define BASE 0x400000u
#define ARENA 0x4000000u
static unsigned char *arena;
static fnv_cpu_function providers[5];static uint32_t values[5],counts[5];
static uint32_t get32(uint32_t at){assert(at>=BASE && (uint64_t)at+4<=BASE+ARENA);unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint32_t at,uint32_t value){assert(at>=BASE && (uint64_t)at+4<=BASE+ARENA);for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(8*i));}
static void capture(unsigned index){
    uint32_t address=index==0 || index==4?fnv_cpu_argument_u32(0):0;
    providers[index]();counts[index]++;
    if(address)values[index]=get32(address)^get32(address+4);
    else {struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);values[index]=state.gpr[0];}
}
static void capture0(void){capture(0);}static void capture1(void){capture(1);}static void capture2(void){capture(2);}
static void capture3(void){capture(3);}static void capture4(void){capture(4);}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config config={0};config.cpu.memory=arena;config.cpu.guest_base=BASE;config.cpu.memory_bytes=ARENA;
    config.cpu.code_low=BASE+0x1000;config.cpu.code_high=BASE+image.size;config.image_bytes=image.size;
    config.peb_address=BASE+0x2000000;config.process_id=0x1711;struct fnv_windows_process *process;
    assert(fnv_windows_create(&config,&process)==FNV_CPU_OK);size_t count;
    const struct fnv_cpu_export *exports=fnv_windows_exports(&count);
    static const char *names[]={"GetSystemTimeAsFileTime","GetCurrentProcessId","GetCurrentThreadId","GetTickCount","QueryPerformanceCounter"};
    for(unsigned i=0;i<5;i++)for(size_t k=0;k<count;k++)if(!exports[k].by_ordinal && !strcmp(exports[k].name,names[i]))providers[i]=exports[k].function;
    for(unsigned i=0;i<5;i++)assert(providers[i]);
    fnv_cpu_function wrappers[]={capture0,capture1,capture2,capture3,capture4};uint32_t tokens[5];
    struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);assert(fnv_cpu_register_native(cpu,wrappers,5,tokens)==FNV_CPU_OK);
    for(unsigned i=0;i<5;i++){void *memory=NULL;assert(fnv_cpu_prepare_memory(cpu,cookie_slots[i],4,&memory)==FNV_CPU_OK);put32(cookie_slots[i],tokens[i]);}
    unsigned cycles=0,seeded=0,initialized=0;
    for(unsigned thread=0;thread<4;thread++){
        struct fnv_windows_thread_config t={0};t.thread_id=0x1234+thread;t.teb_address=BASE+0x2100000+thread*0x20000;
        t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0x14000;
        fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
        for(unsigned iteration=0;iteration<250;iteration++)for(unsigned mode=0;mode<3;mode++){
            uint32_t initial=mode==0?0xbb40e64eu:mode==1?0xaabb1234u:0x1234u;
            put32(FNV_COOKIE_VALUE,initial);put32(FNV_COOKIE_COMPLEMENT,0);memset(values,0,sizeof(values));memset(counts,0,sizeof(counts));
            struct fnv_cpu_state state=t.state;state.gpr[3]=0x11223344;state.gpr[5]=state.seh_ebp=0x13579bdf;
            state.gpr[6]=0x55667788;state.gpr[7]=0x99aabbcc;
            assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);put32(state.gpr[4],0);put32(t.teb_address+0x34,0x12345678);
            struct fnv_cpu_fault fault;enum fnv_cpu_result result=fnv_cpu_execute(FNV_COOKIE_ENTRY,&fault);
            if(result!=FNV_CPU_OK)fprintf(stderr,"cookie failure: %s at %x\n",fault.text,fault.address);
            assert(result==FNV_CPU_OK && fnv_cpu_get_state(&state)==FNV_CPU_OK);
            if(state.gpr[5]!=0x13579bdf)fprintf(stderr,"cookie EBP=%x expected=13579bdf\n",state.gpr[5]);
            assert(state.gpr[4]==t.state.gpr[4]+4 && state.gpr[5]==0x13579bdf && state.seh_ebp==0x13579bdf);
            assert(state.gpr[3]==0x11223344 && state.gpr[6]==0x55667788 && state.gpr[7]==0x99aabbcc);
            assert(state.fs_base==t.teb_address && get32(t.teb_address)==UINT32_MAX && get32(t.teb_address+0x34)==0x12345678);
            uint32_t expected=initial;
            if(mode!=1){
                expected=values[0]^values[1]^values[2]^values[3]^values[4];
                if(expected==0xbb40e64e)expected++;
                else if(!(expected&0xffff0000u))expected|=expected<<16;
                for(unsigned i=0;i<5;i++)assert(counts[i]==1);
                assert(values[1]==config.process_id && values[2]==t.thread_id);initialized++;
            }else{for(unsigned i=0;i<5;i++)assert(!counts[i]);seeded++;}
            assert(get32(FNV_COOKIE_VALUE)==expected && get32(FNV_COOKIE_COMPLEMENT)==~expected);cycles++;
        }
        assert(fnv_windows_thread_detach()==FNV_CPU_OK);
    }
    assert(fnv_cpu_unregister_native(cpu,tokens[0],5)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));printf("{\"status\":\"PASS\",\"captured_cookie_cycles\":%u,\"seeded_cookie_cycles\":%u,\"initialized_cookie_cycles\":%u,\"thread_contexts\":4}\n",cycles,seeded,initialized);return 0;
}
