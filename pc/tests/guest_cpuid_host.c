#include <windows.h>
#include "windows_thread.h"
#include "guest_cpuid.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
int main(int argc,char **argv){
    assert(argc==2 || argc==3);unsigned char *arena=VirtualAlloc(NULL,0x100000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,0x100000,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=0x100000;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.cpu.cpuid=fnv_guest_cpuid_limits;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==1);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_state s=t.state;s.gpr[0]=!strcmp(argv[2],"--strict-basic")?0:!strcmp(argv[2],"--strict-brand")?0x80000002u:0x80000001u;
        assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);struct fnv_cpu_fault f;
        assert(fnv_cpu_execute(0x401800,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_CPUID_UNAVAILABLE);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
        puts("strict PASS");return 0;
    }
    unsigned seed=0x91f7;
    for(unsigned i=0;i<1000;i++){
        seed=seed*1664525u+1013904223u;struct fnv_cpu_state s=t.state,after;
        s.gpr[0]=0x80000000u;s.gpr[1]=seed;s.gpr[2]=~seed;s.gpr[3]=0xdeadbeef;
        s.gpr[5]=BASE+0x18000;s.gpr[6]=0x12345678;s.gpr[7]=0x98765432;
        assert(fnv_cpu_set_state(&s)==FNV_CPU_OK && fnv_cpu_execute(0x401800,NULL)==FNV_CPU_OK && fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[0]==0x80000000u && !after.gpr[1] && !after.gpr[2] && !after.gpr[3]);
        assert(after.gpr[4]==s.gpr[4]+4 && after.gpr[5]==s.gpr[5] && after.gpr[6]==s.gpr[6] && after.gpr[7]==s.gpr[7]);
    }
    uint32_t guard[4]={1,2,3,4};assert(!fnv_guest_cpuid_limits(NULL,1,0,guard) && guard[0]==1 && guard[3]==4);
    assert(!fnv_guest_cpuid_limits(NULL,0x80000000u,0,NULL));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"translated_cpuid_queries\":1000,\"unsupported_leaves_rejected\":true,\"host_features_exposed\":false,\"engine_started\":false}");return 0;
}
