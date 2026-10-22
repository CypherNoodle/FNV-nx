#include <windows.h>
#include <objbase.h>
#include "windows_thread.h"
#include "windows_com.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static struct fnv_windows_process *process;
static _Thread_local uint32_t guest_stack;
static unsigned compared;
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
static void initialize(unsigned api,uint32_t reserved,uint32_t flags,unsigned *count){
    uint32_t args[]={reserved,flags};HRESULT native=api?CoInitializeEx((void *)(uintptr_t)reserved,flags):CoInitialize((void *)(uintptr_t)reserved);
    uint32_t guest=call(api,args,api?2:1);assert(guest==(uint32_t)native);compared++;
    if(SUCCEEDED(native))(*count)++;
    assert(fnv_windows_com_initialization_count()==*count);
}
static void uninitialize(unsigned *count){
    CoUninitialize();call(2,NULL,0);if(*count)(*count)--;
    assert(fnv_windows_com_initialization_count()==*count);
}
static DWORD WINAPI other_thread(void *unused){
    (void)unused;attach(23,0x20000);unsigned count=0;
    /* Fresh OS thread: oracle apartment never inherits its creator's STA. */
    initialize(1,0,0,&count);assert(count==1);uninitialize(&count);
    const uint32_t flags[]={0,2,4,8,12,14,1,3,16,UINT32_MAX};
    for(unsigned i=0;i<1000;i++){
        initialize(1,0,flags[i%10],&count);
        initialize(0,i%7==0?1:0,2,&count);
        initialize(1,0,flags[(i+3)%10],&count);
        while(count)uninitialize(&count);
        uninitialize(&count); /* Unbalanced extra call is a no-op in Windows. */
        initialize(1,0,i&1?0:2,&count);
        initialize(1,0,i&1?2:0,&count); /* Failed model change must not increment. */
        uninitialize(&count);
    }
    /* Metadata does not survive guest detach/reattach on a reused host thread. */
    initialize(0,0,2,&count);CoUninitialize();count=0;
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);attach(24,0x20000);
    initialize(1,0,0,&count);uninitialize(&count);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_com_exports(&n);assert(n==3);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==5);
    attach(22,0x10000);uint32_t args[]={0};assert(call(0,args,1)==0 && fnv_windows_com_initialization_count()==1);
    HANDLE worker=CreateThread(NULL,0,other_thread,NULL,0,NULL);assert(worker && WaitForSingleObject(worker,30000)==WAIT_OBJECT_0 && CloseHandle(worker));
    assert(fnv_windows_com_initialization_count()==1);
    uint32_t mismatch[]={0,0};assert(call(1,mismatch,2)==0x80010106u);
    assert(call(0,args,1)==1 && fnv_windows_com_initialization_count()==2);
    call(2,NULL,0);call(2,NULL,0);assert(fnv_windows_com_initialization_count()==0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_COM_comparisons\":%u,\"thread_isolation\":true,\"detach_reset\":true,\"class_activation_implemented\":false,\"engine_started\":false}\n",compared);return 0;
}
