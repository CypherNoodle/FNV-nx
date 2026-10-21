#include <windows.h>
#include "windows_thread.h"
#include "windows_debug.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define RECORD (BASE+0x80000)
#define NAME_ADDRESS (BASE+0x81000)
static unsigned char *arena;
static char observed[128];static unsigned events;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static LONG CALLBACK event_handler(PEXCEPTION_POINTERS e){
    EXCEPTION_RECORD *r=e->ExceptionRecord;
    if(r->ExceptionCode!=0x406d1388 || (r->ExceptionFlags&EXCEPTION_NONCONTINUABLE) || r->NumberParameters!=4 || r->ExceptionInformation[0]!=0x1000 || r->ExceptionInformation[3])return EXCEPTION_CONTINUE_SEARCH;
    const char *name=(const char *)r->ExceptionInformation[1];assert(strlen(name)<sizeof(observed));strcpy(observed,name);events++;return EXCEPTION_CONTINUE_EXECUTION;
}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_debug_exports(&n);assert(n==1);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && !*fnv_windows_current_debug_name());
    uint32_t args[]={0x406d1388,0,4,RECORD};put(RECORD,0x1000);put(RECORD+4,NAME_ADDRESS);put(RECORD+8,22);put(RECORD+12,0);strcpy((char *)arena+NAME_ADDRESS-BASE,"Main");
    if(argc==3){
        const char *mode=argv[2];
        if(!strcmp(mode,"--strict-code"))args[0]=0xe06d7363;
        else if(!strcmp(mode,"--strict-flags"))args[1]=1;
        else if(!strcmp(mode,"--strict-count"))args[2]=3;
        else if(!strcmp(mode,"--strict-type"))put(RECORD,1);
        else if(!strcmp(mode,"--strict-record"))args[3]=BASE+BYTES-4;
        else if(!strcmp(mode,"--strict-text"))put(RECORD+4,BASE+BYTES);
        else if(!strcmp(mode,"--strict-id"))put(RECORD+8,23);
        else if(!strcmp(mode,"--strict-encoding"))arena[NAME_ADDRESS-BASE]=255;
        else memset(arena+NAME_ADDRESS-BASE,'X',128);
        call(0,args,4,1);
    }else{
        PVOID handler=AddVectoredExceptionHandler(1,event_handler);assert(handler);
        for(unsigned i=0;i<200;i++){
            char name[128];snprintf(name,sizeof(name),"Guest worker %u",i);strcpy((char *)arena+NAME_ADDRESS-BASE,name);put(RECORD+8,i&1?22:UINT32_MAX);
            ULONG_PTR params[]={0x1000,(ULONG_PTR)name,i&1?GetCurrentThreadId():UINT32_MAX,0};
            SetLastError(0x4234);RaiseException(0x406d1388,0,4,params);assert(GetLastError()==0x4234);
            uint32_t error[]={0x4234};call(2,error,1,0);call(0,args,4,0);
            assert(!strcmp(observed,fnv_windows_current_debug_name()) && call(1,NULL,0,0)==0x4234);
        }
        assert(events==200 && RemoveVectoredExceptionHandler(handler));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && !*fnv_windows_current_debug_name());
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"windows_continuing_name_events\":200,\"general_SEH_implemented\":false,\"engine_started\":false}");return 0;
}
