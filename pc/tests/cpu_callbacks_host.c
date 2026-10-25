#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static const char *mode;
static unsigned callbacks;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static void invoke(void){
    uint32_t target=fnv_cpu_argument_u32(0);
    if(target==0x12345678){fnv_cpu_return_u64(UINT64_C(0xfedcba9876543210),4);return;}
    struct fnv_cpu_state before,after;assert(fnv_cpu_get_state(&before)==FNV_CPU_OK);
    uint32_t frame=before.gpr[4]-64;
    put(frame,0x401500);put(frame+4,40);put(frame+8,2);
    uint32_t bytes=8;if(target==0x401950){put(frame+4,7);put(frame+8,0xfffffff0);put(frame+12,0xffffffff);put(frame+16,2);bytes=16;}
    if(mode && !strcmp(mode,"--strict-frame"))frame=before.gpr[4];
    if(mode && !strcmp(mode,"--strict-depth")){
        target=fnv_cpu_import_token(invoke);put(frame+4,target);
    }
    int uses_cdecl=(target==0x401720 || target==0x4017b0 || target==0x401920 || target==0x401950) || (mode && !strcmp(mode,"--strict-cdecl-ret"));
    uint32_t result=uses_cdecl?fnv_cpu_invoke_cdecl(target,frame,bytes):fnv_cpu_invoke_stdcall(target,frame,bytes);
    assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && !memcmp(&before,&after,sizeof(before)));
    callbacks++;fnv_cpu_return_u32(result,4);
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);mode=argc==3?argv[2]:NULL;
    arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_cpu_export exports[]={ {"callback-test.dll","Invoke",0,0,invoke} };
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t bound;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,1,&bound)==FNV_NATIVE_OK && bound==1);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    for(unsigned i=0;i<(mode?1:200);i++){
        struct fnv_cpu_state state,after;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;
        assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);put(state.gpr[4],0x401500);
        const uint32_t targets[4]={0x401700,0x401780,0x401720,0x4017b0};uint32_t target=targets[i%4];
        if(mode){
            if(!strcmp(mode,"--strict-target"))target=0x401b00;
            else if(!strcmp(mode,"--strict-ret"))target=0x401800;
            else if(!strcmp(mode,"--strict-register"))target=0x401880;
            else if(!strcmp(mode,"--strict-instruction"))target=0x401900;
            else if(!strcmp(mode,"--strict-cdecl-ret"))target=0x401700;
            else if(!strcmp(mode,"--strict-cdecl-register"))target=0x401920;
        }
        put(state.gpr[4]+4,target);struct fnv_cpu_fault fault;
        enum fnv_cpu_result r=fnv_cpu_execute(0x401500,&fault);
        if(mode){assert(r==FNV_CPU_GUEST_FAULT);assert(fnv_cpu_is_faulted(fnv_windows_runtime(process)));break;}
        if(r!=FNV_CPU_OK)fprintf(stderr,"callback fault %s\n",fault.text);
        assert(r==FNV_CPU_OK && fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[4]==state.gpr[4]+8 && after.gpr[0]==(i&1?45u:42u));
    }
    if(!mode){
        struct fnv_cpu_state state,after;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;
        assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);put(state.gpr[4],0x401500);put(state.gpr[4]+4,0x401950);
        struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500,&fault)==FNV_CPU_OK);
        assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[0]==UINT32_MAX && after.gpr[4]==state.gpr[4]+8);
    }
    if(!mode){
        struct fnv_cpu_state state,after;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;
        assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);put(state.gpr[4],0x401500);put(state.gpr[4]+4,0x12345678);
        struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500,&fault)==FNV_CPU_OK);
        assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[0]==0x76543210 && after.gpr[2]==0xfedcba98 && after.gpr[4]==state.gpr[4]+8);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));
    if(mode){puts("strict PASS");return 0;}
    assert(callbacks==301);
    puts("{\"status\":\"PASS\",\"callbacks\":301,\"nested_pairs\":100,\"engine_started\":false}");return 0;
}
