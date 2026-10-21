#include <windows.h>
#include "windows_system.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static uint32_t permissions(void *context,void *address,uint32_t bytes,uint32_t protection){(void)context;(void)address;(void)bytes;(void)protection;return 0;}
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
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
    assert(argc==2 || argc==3);unsigned checks=0;
    for(unsigned profile=0;profile<(argc==3?1u:3u);profile++){
        const uint32_t counts[]={1,3,32};uint32_t processors=counts[profile];
        arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
        struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
        struct fnv_guest_memory *vm;struct fnv_vm_config v={0};v.backing=arena+0x80000;v.base=BASE+0x80000;v.bytes=0x80000;v.commit_limit=v.bytes;
        struct fnv_vm_permissions backend={NULL,permissions};v.context=&backend;v.apply=fnv_vm_eager_apply;
        assert(!fnv_vm_create(&v,&vm).error);
        struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.memory=vm;
        c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
        struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
        struct fnv_windows_system *system,*other=(void *)1;
        assert(fnv_windows_system_create(process,0,&other)==FNV_CPU_INVALID && !other);
        assert(fnv_windows_system_create(process,33,&other)==FNV_CPU_INVALID && !other);
        assert(fnv_windows_system_create(process,processors,&system)==FNV_CPU_OK);
        assert(fnv_windows_system_create(process,1,&other)==FNV_CPU_BUSY && !other);
        size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_system_exports(&n);assert(n==1 && exports==fnv_windows_system_exports(NULL));
        struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
        assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
        struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
        t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
        assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t answer=0;
        assert(fnv_windows_system_destroy(system)==FNV_CPU_BUSY);
        struct fnv_vm_result alloc=fnv_vm_allocate(vm,0,4096,0x3000,4);assert(!alloc.error);
        if(argc==3){uint32_t at=!strcmp(argv[2],"--strict-null")?0:!strcmp(argv[2],"--strict-boundary")?BASE+BYTES-35:alloc.value;
            if(!strcmp(argv[2],"--strict-readonly")){uint32_t old;assert(!fnv_vm_protect(vm,at,4096,2,&old).error);}
            if(!strcmp(argv[2],"--strict-uncommitted"))at=BASE+0x90000;
            struct fnv_cpu_fault fault;memset(arena+0x80000,0x7B,0x80000);
            assert(call(0,at,&answer,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED && fault.address==at);
            assert(arena[0x80000]==0x7B && arena[BYTES-1]==0x7B);
            assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_system_destroy(system)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
            assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
        }
        for(unsigned i=0;i<1000;i++){
            uint32_t at=i&1?alloc.value+1:BASE+0xA001,marker=0x1837+i;
            memset(arena+at-BASE-1,0xA6,38);assert(call(2,marker,&answer,NULL)==FNV_CPU_OK);
            assert(call(0,at,&answer,NULL)==FNV_CPU_OK);
            uint32_t expected[]={0,4096,BASE,BASE+BYTES-1,processors==32?UINT32_MAX:(1u<<processors)-1,processors,586,65536,6};
            for(unsigned j=0;j<9;j++)assert(read32(at+j*4)==expected[j]);
            assert(arena[at-BASE-1]==0xA6 && arena[at-BASE+36]==0xA6);
            assert(call(1,0,&answer,NULL)==FNV_CPU_OK && answer==marker);checks++;
        }
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_system_destroy(system)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));
    }
    SYSTEM_INFO native;GetSystemInfo(&native);assert(native.dwPageSize==4096 && native.dwAllocationGranularity==65536);
    printf("{\"status\":\"PASS\",\"system_info_checks\":%u,\"topology_profiles\":3,\"output_guards\":true,\"stdcall_checked\":true,\"native_engine_started\":false}\n",checks);return 0;
}
