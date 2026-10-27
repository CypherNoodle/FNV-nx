#include <windows.h>
#include <objbase.h>
#include "windows_thread.h"
#include "windows_com.h"
#include "windows_com_diag.h"
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
static void put(uint32_t,uint32_t);
static uint32_t call(unsigned,const uint32_t *,unsigned);
static unsigned activations,traces;
static struct fnv_com_activation last_activation;
static void trace(void *context,const struct fnv_com_activation *record){
    assert(context==&activations);last_activation=*record;traces++;
}
static void activation(uint32_t context,int null_output,int aggregate){
    GUID clsid={0x31415926,0x5358,0x9793,{0x23,0x84,0x62,0x64,0x33,0x83,0x27,0x95}};
    GUID iid={0};iid.Data4[0]=0xc0;iid.Data4[7]=0x46;
    const uint32_t c=BASE+0x7000,i=c+16,o=c+32;
    memcpy(arena+c-BASE,&clsid,16);memcpy(arena+i-BASE,&iid,16);put(o,0x12345678);
    void *native_output=(void *)(uintptr_t)0x12345678;
    HRESULT native=CoCreateInstance(&clsid,aggregate?(IUnknown *)(uintptr_t)1:NULL,context,&iid,null_output?NULL:&native_output);
    uint32_t args[]={c,aggregate?1u:0u,context,i,null_output?0:o};
    uint32_t guest=call(5,args,5);if(guest!=(uint32_t)native)fprintf(stderr,"activation mismatch ctx=%x null=%d outer=%d com=%u guest=%x native=%lx\n",context,null_output,aggregate,fnv_windows_com_initialization_count(),guest,(unsigned long)native);assert(guest==(uint32_t)native);
    uint32_t output;memcpy(&output,arena+o-BASE,4);
    assert(output==(null_output?0x12345678:(uint32_t)(uintptr_t)native_output));
    assert(last_activation.result==guest && last_activation.context==context && last_activation.outer==args[1]);
    if(!null_output)assert(!memcmp(last_activation.clsid,&clsid,16) && !memcmp(last_activation.iid,&iid,16));
    activations++;assert(traces==activations);
}
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
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_com_exports(&n);assert(n==4);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==6);
    fnv_windows_com_activation_trace(&activations,trace);
    attach(22,0x10000);activation(1,0,0);activation(1,1,0);
    uint32_t args[]={0};assert(call(0,args,1)==0 && fnv_windows_com_initialization_count()==1);
    assert(SUCCEEDED(CoInitialize(NULL)));
    const uint32_t contexts[]={0,1,2,3,4,5,7,16,23,32,0x80000000u,UINT32_MAX};
    for(unsigned k=0;k<1000;k++)activation(contexts[k%12],k%13==0,k%7==0);
    CoUninitialize();
    HANDLE worker=CreateThread(NULL,0,other_thread,NULL,0,NULL);assert(worker && WaitForSingleObject(worker,30000)==WAIT_OBJECT_0 && CloseHandle(worker));
    assert(fnv_windows_com_initialization_count()==1);
    uint32_t mismatch[]={0,0};assert(call(1,mismatch,2)==0x80010106u);
    assert(call(0,args,1)==1 && fnv_windows_com_initialization_count()==2);
    call(2,NULL,0);call(2,NULL,0);assert(fnv_windows_com_initialization_count()==0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    fnv_windows_com_activation_trace(NULL,NULL);printf("COM activation rejection comparisons=%u\n",activations);
    printf("{\"status\":\"PASS\",\"windows_COM_comparisons\":%u,\"thread_isolation\":true,\"detach_reset\":true,\"class_activation_implemented\":false,\"engine_started\":false}\n",compared);return 0;
}
