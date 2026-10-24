#include <windows.h>
#include "windows_filetime.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x200000u
#define LEFT (BASE+0x50001)
#define RIGHT (BASE+0x50013)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,int negative){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);put(s.gpr[4]+4,a);put(s.gpr[4]+8,b);struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT&&fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    assert(r==FNV_CPU_OK&&fnv_cpu_get_state(&after)==FNV_CPU_OK&&after.gpr[4]==s.gpr[4]+4+(api==0?8:api==1?0:4));return after.gpr[0];
}
static uint32_t permissions(void *ctx,void *at,uint32_t bytes,uint32_t p){(void)ctx;(void)at;(void)bytes;(void)p;return 0;}
int main(int argc,char **argv){
    assert(argc==2||argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_vm_permissions permission={NULL,permissions};struct fnv_vm_config v={arena+0x100000,BASE+0x100000,0x100000,0x100000,&permission,fnv_vm_eager_apply};struct fnv_guest_memory *vm;assert(!fnv_vm_create(&v,&vm).error);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;c.memory=vm;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_filetime_exports(&n);assert(n==1);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK&&bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){call(0,!strcmp(argv[2],"--strict-null")?0:BASE+0x100000,RIGHT,1);goto finish;}
    const uint64_t edge[]={0,1,0xffffffffull,0x100000000ull,0x7fffffffffffffffull,0x8000000000000000ull,UINT64_MAX};
    uint64_t seed=0x872379aull;unsigned checked=0;
    for(unsigned i=0;i<5000+49;i++){
        uint64_t a,b;if(i<49){a=edge[i/7];b=edge[i%7];}else{seed=seed*6364136223846793005ull+1;a=seed;seed=seed*6364136223846793005ull+1;b=i%3?seed:a;}
        FILETIME x={(DWORD)a,(DWORD)(a>>32)},y={(DWORD)b,(DWORD)(b>>32)};memcpy(arena+LEFT-BASE,&x,8);memcpy(arena+RIGHT-BASE,&y,8);
        SetLastError(0x4234);LONG expected=CompareFileTime(&x,&y);DWORD error=GetLastError();call(2,0x4234,0,0);
        assert(call(0,LEFT,RIGHT,0)==(uint32_t)expected&&call(1,0,0,0)==error);assert(!memcmp(arena+LEFT-BASE,&x,8)&&!memcmp(arena+RIGHT-BASE,&y,8));checked++;
    }
    assert(call(0,LEFT,LEFT,0)==0);
    printf("{\"status\":\"PASS\",\"native_windows_comparisons\":%u,\"unaligned_and_high_bit\":true,\"engine_started\":false}\n",checked);
finish:
    assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK&&!fnv_vm_destroy(vm).error&&VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3)puts("strict PASS");return 0;
}
