#include <windows.h>
#include <mmsystem.h>
#include "windows_thread.h"
#include "windows_multimedia.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static struct fnv_windows_process *process;
static _Thread_local uint32_t guest_stack;

static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=guest_stack;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*32,&fault)==FNV_CPU_OK);
    assert(fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
static void attach(uint32_t id,uint32_t offset){
    struct fnv_windows_thread_config t={0};t.thread_id=id;t.teb_address=BASE+offset;t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;
    guest_stack=t.stack_high-256;fnv_cpu_state_init(&t.state,guest_stack,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(fnv_windows_com_initialization_count()==0);
}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_multimedia_exports(&n);assert(n==1);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==4);
    attach(22,0x10000);uint32_t error[]={0x12345678};call(3,error,1);
    for(unsigned i=0;i<2000;i++){
        DWORD before=timeGetTime();uint32_t value=call(0,NULL,0),tick=call(1,NULL,0);DWORD after=timeGetTime();
        assert((int32_t)(value-before)>=-32 && (int32_t)(after-value)>=-32 && (int32_t)(tick-value)>=0 && (uint32_t)(tick-value)<1000);
        assert(call(2,NULL,0)==error[0]);if(!(i%200))Sleep(1);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"windows_uptime_checks\":2000,\"last_error_preserved\":true,\"timer_resolution_changed\":false,\"engine_started\":false}");return 0;
}
