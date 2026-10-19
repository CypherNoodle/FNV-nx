#include <windows.h>
#include "windows_memory.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include "fnv_sdk_runtime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define BASE 0x400000u
#define ARENA (64u<<20)
#define POOL_BASE 0x2000000u
#define POOL_BYTES (16u<<20)
#define OUTPUT (BASE+0x9000)
enum { ALLOCATE,FREE_MEMORY,QUERY,PROTECT,GET_ERROR,SET_ERROR,MEMORY_STATUS,READ_MEMORY,WRITE_MEMORY };
static unsigned char *arena;
static struct fnv_guest_memory *vm;
static struct fnv_windows_process *process;
static struct fnv_native_image image;
static struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
static unsigned permission_calls,fail_at,fail_again;
static uint32_t permissions(void *context,void *address,uint32_t bytes,uint32_t permission) {
    (void)context;permission_calls++;
    if(permission_calls==fail_at || permission_calls==fail_again)return 0xCE01;
    DWORD old;return VirtualProtect(address,bytes,permission==3?PAGE_READWRITE:permission==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();
}
static struct fnv_vm_permissions backend={NULL,permissions};
static uint32_t read32(uint32_t at) {unsigned char *p=arena+(at-BASE);return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t n) {for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(n>>(i*8));}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d) {
    static const unsigned argc[]={4,3,3,4,0,1,1,0,0};
    struct fnv_cpu_state s,got;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    s.gpr[4]=BASE+0x1BF00;s.gpr[0]=a;s.gpr[3]=b;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    uint32_t values[]={0x401500,a,b,c,d};
    for(unsigned i=0;i<5;i++)write32(s.gpr[4]+i*4,values[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*0x20,&fault);
    if(result!=FNV_CPU_OK)fprintf(stderr,"VM API %u fault %u %s\n",api,fault.kind,fault.text);
    assert(result==FNV_CPU_OK && fnv_cpu_get_state(&got)==FNV_CPU_OK);
    assert(got.gpr[4]==s.gpr[4]+4+argc[api]*4 && got.fs_base==s.fs_base);return got.gpr[0];
}
static void create(const char *fixture) {
    assert(fnv_native_load_image(fixture,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_vm_config m={arena+(POOL_BASE-BASE),POOL_BASE,POOL_BYTES,POOL_BYTES,&backend,fnv_vm_eager_apply};
    assert(!fnv_vm_create(&m,&vm).error);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;
    c.peb_address=BASE+0x8000;c.process_id=11;c.memory=vm;
    struct fnv_windows_process *invalid;
    c.cpu.memory=arena+4096;assert(fnv_windows_create(&c,&invalid)==FNV_CPU_INVALID && !invalid);c.cpu.memory=arena;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK && fnv_vm_destroy(vm).error==170);
    size_t n,count;const struct fnv_cpu_export *exports=fnv_windows_memory_exports(&n);
    assert(n==5 && fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&count)==FNV_NATIVE_OK && count==7);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    struct fnv_windows_thread_config bad=t;bad.teb_address=POOL_BASE;bad.state.fs_base=POOL_BASE;
    assert(fnv_windows_thread_attach(process,&bad)==FNV_CPU_INVALID);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
}
static void close_process(void) {
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    fail_at=fail_again=0;assert(!fnv_vm_destroy(vm).error);
}
static uint64_t read64(uint32_t at) {return read32(at)|((uint64_t)read32(at+4)<<32);}
static void memory_status(void) {
    _Static_assert(sizeof(MEMORYSTATUSEX)==64 && offsetof(MEMORYSTATUSEX,ullTotalPhys)==8 &&
        offsetof(MEMORYSTATUSEX,ullAvailExtendedVirtual)==56,"MEMORYSTATUSEX ABI");
    const uint32_t invalid[]={0,1,32,63,65,128,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        MEMORYSTATUSEX native;memset(&native,0xA5,sizeof(native));native.dwLength=invalid[i];
        MEMORYSTATUSEX before=native;SetLastError(777);
        BOOL result=GlobalMemoryStatusEx(&native);DWORD error=GetLastError();
        assert(!result && error==87 && !memcmp(&native,&before,sizeof(native)));
        memcpy(arena+OUTPUT-BASE,&before,64);call(SET_ERROR,777,0,0,0);
        assert(call(MEMORY_STATUS,OUTPUT,0,0,0)==0 && call(GET_ERROR,0,0,0,0)==error);
        assert(!memcmp(arena+OUTPUT-BASE,&before,64));
    }
    /* Byte-oriented guest ABI must also work at an unaligned address. */
    uint32_t at=OUTPUT+1;write32(at-1,0x5A5A5A5A);write32(at+64,0xB6B6B6B6);write32(at,64);
    call(SET_ERROR,777,0,0,0);assert(call(MEMORY_STATUS,at,0,0,0)==1 && call(GET_ERROR,0,0,0,0)==777);
    MEMORYSTATUSEX native={0};native.dwLength=64;assert(GlobalMemoryStatusEx(&native));
    assert(read32(at)==64 && read32(at+4)<=100 && read64(at+8)==native.ullTotalPhys);
    assert(read64(at+16)<=read64(at+8) && read64(at+24)==POOL_BYTES && read64(at+32)==POOL_BYTES);
    assert(read64(at+40)==ARENA && read64(at+48)==POOL_BYTES && !read64(at+56));
    assert(arena[at-BASE-1]==0x5A && read32(at+64)==0xB6B6B6B6);
    uint32_t block=call(ALLOCATE,0,65536,FNV_VM_RESERVE,4);assert(block);
    assert(call(ALLOCATE,block,4096,FNV_VM_COMMIT,4)==block);
    write32(at,64);assert(call(MEMORY_STATUS,at,0,0,0)==1);
    assert(read64(at+32)==POOL_BYTES-4096 && read64(at+48)==POOL_BYTES-65536);
    write32(block,64);assert(call(PROTECT,block,4096,2,OUTPUT+128)==1);
    assert(call(MEMORY_STATUS,block,0,0,0)==0 && call(GET_ERROR,0,0,0,0)==998);
    assert(call(FREE_MEMORY,block,0,FNV_VM_RELEASE,0)==1);
    write32(at,64);assert(call(MEMORY_STATUS,at,0,0,0)==1);
    assert(read64(at+32)==POOL_BYTES && read64(at+48)==POOL_BYTES);
}
static void abi(void) {
    call(SET_ERROR,777,0,0,0);uint32_t base=call(ALLOCATE,0,65536,FNV_VM_RESERVE,4);
    assert(base==POOL_BASE && call(GET_ERROR,0,0,0,0)==777);
    assert(call(QUERY,base,OUTPUT,28,0)==28 && read32(OUTPUT)==base && read32(OUTPUT+16)==FNV_VM_RESERVE);
    assert(read32(OUTPUT+12)==65536 && read32(OUTPUT+20)==0 && read32(OUTPUT+24)==FNV_VM_PRIVATE);
    memset(arena+(OUTPUT-BASE),0xA5,32);assert(!call(QUERY,base,OUTPUT,27,0));
    assert(call(GET_ERROR,0,0,0,0)==87 && read32(OUTPUT)==0xA5A5A5A5);
    assert(!call(QUERY,base,base,28,0) && call(GET_ERROR,0,0,0,0)==998);
    assert(call(ALLOCATE,base,8192,FNV_VM_COMMIT,4)==base);
    assert(call(READ_MEMORY,base,0,0,0)==0);call(WRITE_MEMORY,base,0x12345678,0,0);
    assert(call(READ_MEMORY,base,0,0,0)==0x12345678);
    assert(call(PROTECT,base,4096,2,OUTPUT)==1 && read32(OUTPUT)==4);
    assert(call(READ_MEMORY,base,0,0,0)==0x12345678);
    assert(!call(QUERY,base,base,28,0) && call(GET_ERROR,0,0,0,0)==998);
    assert(call(ALLOCATE,base,4096,FNV_VM_COMMIT,4)==base && call(READ_MEMORY,base,0,0,0)==0x12345678);
    assert(!call(PROTECT,base,4096,2,base) && call(GET_ERROR,0,0,0,0)==50);
    assert(call(FREE_MEMORY,base+4095,2,FNV_VM_DECOMMIT,0));
    assert(call(ALLOCATE,base,8192,FNV_VM_COMMIT,4)==base && call(READ_MEMORY,base,0,0,0)==0);
    assert(!call(FREE_MEMORY,base+4096,0,FNV_VM_RELEASE,0));
    assert(call(FREE_MEMORY,base,0,FNV_VM_RELEASE,0));
    assert(call(QUERY,base,OUTPUT,28,0)==28 && read32(OUTPUT+16)==FNV_VM_FREE);
    fail_at=permission_calls+1;
    assert(!call(ALLOCATE,base,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,4) && call(GET_ERROR,0,0,0,0)==8);
    fail_at=0;assert(call(QUERY,base,OUTPUT,28,0)==28 && read32(OUTPUT+16)==FNV_VM_FREE);
    assert(!call(ALLOCATE,0,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,0x40) && call(GET_ERROR,0,0,0,0)==50);
}
static void strict_fault(int outside_query) {
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1BF00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);write32(s.gpr[4],0x401500);
    unsigned api;
    if(outside_query){api=QUERY;write32(s.gpr[4]+4,BASE);write32(s.gpr[4]+8,OUTPUT);write32(s.gpr[4]+12,28);}
    else {api=ALLOCATE;uint32_t base=call(ALLOCATE,0,8192,FNV_VM_RESERVE|FNV_VM_COMMIT,4);
        assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);write32(s.gpr[4]+4,base);write32(s.gpr[4]+8,8192);
        write32(s.gpr[4]+12,FNV_VM_COMMIT);write32(s.gpr[4]+16,2);fail_at=permission_calls+2;fail_again=permission_calls+3;}
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*0x20,&fault)==FNV_CPU_GUEST_FAULT);
    assert(fault.kind==FNV_CPU_UNIMPLEMENTED && strstr(fault.text,outside_query?"outside":"rollback"));
    assert(fnv_cpu_execute(0x401500,NULL)==FNV_CPU_FAULTED);
}
static LONG WINAPI access_violation(EXCEPTION_POINTERS *p) {
    uintptr_t address=p->ExceptionRecord->ExceptionInformation[1];
    if(p->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
       address>=(uintptr_t)arena+(POOL_BASE-BASE) && address<(uintptr_t)arena+(POOL_BASE-BASE)+POOL_BYTES)_Exit(77);
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(int argc,char **argv) {
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    create(argv[1]);
    if(argc==3){assert(AddVectoredExceptionHandler(1,access_violation));
        if(!strcmp(argv[2],"--readonly")){assert(call(ALLOCATE,POOL_BASE,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,2));call(WRITE_MEMORY,POOL_BASE,1,0,0);}
        else call(READ_MEMORY,POOL_BASE,0,0,0);
        return 9;}
    memory_status();abi();strict_fault(1);close_process();create(argv[1]);strict_fault(0);close_process();
    assert(VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"memory_exports\":5,\"iat_calls_and_mbi32\":true,\"memory_status_native_oracle\":true,\"memory_status_vm_accounting\":true,\"strict_faults\":2,\"native_engine_started\":false}");return 0;
}
