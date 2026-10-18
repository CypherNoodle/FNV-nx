#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
static unsigned char *arena;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;struct fnv_windows_process *process;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==2);
    for(unsigned index=0;index<4;++index){
        struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x20000;t.tls_expansion_address=t.teb_address+4096;
        t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
        assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
        for(unsigned i=0;i<250;++i){
            struct fnv_cpu_state s=t.state;s.gpr[5]=s.seh_ebp=0x13579bdf;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0);
            put32(t.teb_address+0x34,0x789abc);struct fnv_cpu_fault fault;
            enum fnv_cpu_result result=fnv_cpu_execute(0x401800,&fault);if(result!=FNV_CPU_OK)fprintf(stderr,"SEH frame: %s\n",fault.text);
            assert(result==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK);
            uint32_t out=BASE+0x9000+t.thread_id*32,outer=read32(out),inner=read32(out+12);
            assert(outer>=t.stack_low && outer+8<t.stack_high && inner<outer && inner>=t.stack_low);
            assert(read32(out+4)==UINT32_MAX && read32(out+8)==outer && read32(out+16)==outer);
            assert(read32(outer+4)==0x4019e0 && read32(inner+4)==0x4019e0);
            assert(read32(t.teb_address)==UINT32_MAX && read32(t.teb_address+0x34)==0x789abc);
            if(s.gpr[4]!=t.state.gpr[4]+4 || s.gpr[5]!=0x13579bdf || s.fs_base!=t.teb_address)fprintf(stderr,"final esp=%x expected=%x ebp=%x fs=%x\n",s.gpr[4],t.state.gpr[4]+4,s.gpr[5],s.fs_base);
            assert(s.gpr[4]==t.state.gpr[4]+4 && s.gpr[5]==0x13579bdf && s.fs_base==t.teb_address);
        }
        assert(fnv_windows_thread_detach()==FNV_CPU_OK);
    }
    assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"normal_nested_frame_cycles\":1000,\"thread_contexts\":4,\"exception_dispatch_implemented\":false,\"native_engine_started\":false}");return 0;
}
