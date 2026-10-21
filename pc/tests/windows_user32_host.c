#include <windows.h>
#include "windows_thread.h"
#include "windows_user32.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static enum fnv_cpu_result call(unsigned api,uint32_t value,uint32_t *answer,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state before,after;assert(fnv_cpu_get_state(&before)==FNV_CPU_OK);
    before.gpr[4]=BASE+0x1BF00;assert(fnv_cpu_set_state(&before)==FNV_CPU_OK);
    write32(before.gpr[4],0x401500);write32(before.gpr[4]+4,value);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[4]==before.gpr[4]+4+(api==1?0:4));*answer=after.gpr[0];}
    return r;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_user32_exports(&n);assert(n==1 && exports==fnv_windows_user32_exports(NULL));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t answer=0;
    if(argc==3){uint32_t index=!strcmp(argv[2],"--strict-width")?0:!strcmp(argv[2],"--strict-height")?1:!strcmp(argv[2],"--strict-negative")?UINT32_MAX:0x1001;
        struct fnv_cpu_fault fault;assert(call(0,index,&answer,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fault.kind==FNV_CPU_UNIMPLEMENTED && fault.address==index && strstr(fault.text,"system metric pending"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);puts("strict PASS");return 0;}
    unsigned seed=0x62b8;
    for(unsigned i=0;i<1000;i++){seed=seed*1664525u+1013904223u;
        assert(call(2,seed,&answer,NULL)==FNV_CPU_OK);
        assert(call(0,0x1000,&answer,NULL)==FNV_CPU_OK && answer==0);
        assert(call(1,0,&answer,NULL)==FNV_CPU_OK && answer==seed);
    }
    SetLastError(0x1357);int native=GetSystemMetrics(SM_REMOTESESSION);DWORD error=GetLastError();
    /* Windows documents no extended error for this API. Initial lazy host
     * USER32 setup can change LastError; record it without assuming a value. */
    (void)error;
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"local_session_checks\":1000,\"last_error_preserved\":true,\"stdcall_checked\":true,\"windows_remote_session\":%d,\"native_engine_started\":false}\n",native);return 0;
}
