#include <windows.h>
#include "windows_thread.h"
#include "windows_registry.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t get(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)arena[at-BASE+i]<<(8*i);return v;}
static enum fnv_cpu_result call(unsigned api,const uint32_t *args,uint32_t *answer){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put(s.gpr[4],0x401500);
    unsigned count=api?1:5;for(unsigned i=0;i<count;i++)put(s.gpr[4]+4+4*i,args[i]);
    put(BASE+0x10034,0x12345678);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK);assert(after.gpr[4]==s.gpr[4]+4+4*count);
        assert(get(BASE+0x10034)==0x12345678);*answer=after.gpr[0];}
    else assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    return r;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_registry_exports(&n);assert(n==2);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==2);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    const char *missing="Software\\FNV_SYNTHETIC_MISSING_17E827E4";strcpy((char *)arena+0x80000,missing);
    uint32_t args[]={0x80000001u,BASE+0x80000,0,KEY_READ,BASE+0x81000},answer;
    if(argc==3){
        if(!strcmp(argv[2],"--strict-root"))args[0]=123;
        else if(!strcmp(argv[2],"--strict-options"))args[2]=8;
        else if(!strcmp(argv[2],"--strict-access"))args[3]=0xffffffff;
        else if(!strcmp(argv[2],"--strict-output"))args[4]=BASE+BYTES-3;
        else if(!strcmp(argv[2],"--strict-input"))args[1]=BASE+BYTES;
        else if(!strcmp(argv[2],"--strict-encoding"))arena[0x80000]=0xff;
        else if(!strcmp(argv[2],"--strict-length"))memset(arena+0x80000,'a',1024);
        else assert(0);
        assert(call(0,args,&answer)==FNV_CPU_GUEST_FAULT);puts("strict PASS");
    }else{
        const uint32_t access[]={KEY_READ,KEY_ALL_ACCESS,KEY_READ|KEY_WOW64_32KEY,KEY_READ|KEY_WOW64_64KEY};
        for(unsigned i=0;i<1000;i++){
            args[3]=access[i%4];HKEY native=(HKEY)(uintptr_t)0x12345678;SetLastError(0x12345678);
            LSTATUS result=RegOpenKeyExA(HKEY_CURRENT_USER,missing,0,args[3],&native);
            assert(result==ERROR_FILE_NOT_FOUND && native==NULL && GetLastError()==0x12345678);
            put(args[4],0x12345678);assert(call(0,args,&answer)==FNV_CPU_OK && answer==(uint32_t)result && get(args[4])==0);
        }
        for(unsigned i=0;i<1000;++i){uint32_t null_key=0;SetLastError(0x12345678);
            LSTATUS result=RegCloseKey(NULL);assert(result==ERROR_INVALID_HANDLE && GetLastError()==0x12345678);
            assert(call(1,&null_key,&answer)==FNV_CPU_OK && answer==(uint32_t)result);}
        uint32_t roots[]={0x80000000u,0x80000001u,0x80000002u,0x80000003u,0x80000005u};
        for(unsigned i=0;i<5;i++)for(unsigned empty=0;empty<2;empty++){
            args[0]=roots[i];args[1]=empty?BASE+0x80000:0;arena[0x80000]=0;args[3]=KEY_READ;
            assert(call(0,args,&answer)==FNV_CPU_OK && answer==0 && get(args[4])==roots[i]);
            assert(call(1,args,&answer)==FNV_CPU_OK && answer==0);
        }
        puts("{\"status\":\"PASS\",\"windows_missing_key_comparisons\":1000,\"invalid_close_comparisons\":1000,\"virtual_root_checks\":20,\"engine_started\":false}");
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));return 0;
}
