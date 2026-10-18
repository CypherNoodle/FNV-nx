#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
enum {STD_HANDLE,GET_ERROR,SET_ERROR,STARTUP_A,STARTUP_W,HANDLE_COUNT};
static unsigned char *arena;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t call(unsigned api,uint32_t arg){
    struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;
    state.gpr[3]=0x12345678;state.gpr[5]=0x23456789;state.gpr[6]=0x3456789a;state.gpr[7]=0x456789ab;
    assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);write32(state.gpr[4],0x401500);write32(state.gpr[4]+4,arg);
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*32,&fault)==FNV_CPU_OK);
    assert(fnv_cpu_get_state(&state)==FNV_CPU_OK && state.gpr[4]==BASE+0x1bf04+(api==GET_ERROR?0:4));
    assert(state.gpr[3]==0x12345678 && state.gpr[5]==0x23456789 && state.gpr[6]==0x3456789a && state.gpr[7]==0x456789ab);
    return state.gpr[0];
}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==6);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    const uint32_t selectors[]={-10u,-11u,-12u,0,1,-1u,-9u,-13u,0x12345678};
    const uint32_t seeds[]={0,0x3456,0xdeadbeef};unsigned checks=0;
    uint32_t observed[27][3];unsigned sample=0;
    for(unsigned repeat=0;repeat<64;repeat++)for(unsigned seed=0;seed<3;seed++)for(unsigned i=0;i<9;i++){
        call(SET_ERROR,seeds[seed]);uint32_t value=call(STD_HANDLE,selectors[i]),error=call(GET_ERROR,0);
        assert(value==(i<3?0:UINT32_MAX) && error==(i<3?seeds[seed]:6));checks++;
        if(!repeat){observed[sample][0]=selectors[i];observed[sample][1]=value;observed[sample++][2]=error;}
    }
    for(unsigned api=STARTUP_A;api<=STARTUP_W;api++){
        uint32_t out=BASE+0x9004;memset(arena+out-BASE-4,0xaa,76);call(SET_ERROR,0x3456);call(api,out);
        assert(read32(out-4)==0xaaaaaaaa && read32(out+68)==0xaaaaaaaa && call(GET_ERROR,0)==0x3456);
        assert(read32(out)==68 && !(read32(out+44)&0x100) && !read32(out+56) && !read32(out+60) && !read32(out+64));
    }
    const uint32_t requests[]={0,1,3,20,512,UINT32_MAX,0x80000000};uint32_t counts[21][3];unsigned count_checks=0,n=0;
    for(unsigned repeat=0;repeat<64;repeat++)for(unsigned seed=0;seed<3;seed++)for(unsigned i=0;i<7;i++){
        call(SET_ERROR,seeds[seed]);uint32_t value=call(HANDLE_COUNT,requests[i]),error=call(GET_ERROR,0);
        assert(value==requests[i] && error==seeds[seed]);count_checks++;
        if(!repeat){counts[n][0]=requests[i];counts[n][1]=value;counts[n++][2]=error;}
        for(unsigned j=0;j<3;j++)assert(!call(STD_HANDLE,selectors[j]) && call(GET_ERROR,0)==seeds[seed]);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"translated_checks\":%u,\"startup_handles_A_W_consistent\":true,\"samples\":[",checks);
    for(unsigned i=0;i<sample;i++)printf("%s[%u,%u,%u]",i?",":"",observed[i][0],observed[i][1],observed[i][2]);
    printf("],\"handle_count_checks\":%u,\"handle_counts\":[",count_checks);
    for(unsigned i=0;i<n;i++)printf("%s[%u,%u,%u]",i?",":"",counts[i][0],counts[i][1],counts[i][2]);
    puts("],\"native_engine_started\":false}");return 0;
}
