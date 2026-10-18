#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define CTX (BASE+0x9000u)
#define OUTPUT (BASE+0x9800u)
enum {CREATE,EXIT,CODE,WAIT,CLOSE,TID,HID,SELF,RESUME,SLEEP,SET_ERROR,GET_ERROR,TLS_SET,TLS_GET,MUTEX,MANY,TLS_ALLOC};
static const unsigned counts[]={6,1,2,2,1,0,1,0,1,1,1,0,2,1,3,4,0};
static unsigned char *arena;
static struct fnv_windows_process *process;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t n){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(n>>(i*8));}
static enum fnv_cpu_result execute(unsigned api,const uint32_t args[6],struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    write32(s.gpr[4],0x401500);for(unsigned i=0;i<6;++i)write32(s.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*64,fault);
    if(result==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+counts[api]*4);}return result;
}
static uint32_t call6(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f){
    uint32_t args[]={a,b,c,d,e,f};struct fnv_cpu_fault fault;enum fnv_cpu_result result=execute(api,args,&fault);
    if(result!=FNV_CPU_OK)fprintf(stderr,"worker API %u result %u: %s\n",api,(unsigned)result,fault.text);
    struct fnv_cpu_state s;assert(result==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK);return s.gpr[0];
}
#define call(api,a,b,c,d) call6(api,a,b,c,d,0,0)
static struct fnv_windows_thread_config main_config(void){struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;}
static struct fnv_windows_worker_stats stats(void){struct fnv_windows_worker_stats s;assert(fnv_windows_worker_stats(process,&s)==FNV_CPU_OK);return s;}
static void settle(void){ULONGLONG deadline=GetTickCount64()+5000;while(stats().active && GetTickCount64()<deadline)Sleep(1);assert(!stats().active);}
static uint32_t spawn(uint32_t entry,uint32_t parameter,uint32_t flags){return call6(CREATE,0,0,entry,parameter,flags,OUTPUT);}
static void context(uint32_t at,uint32_t tls,uint32_t code,uint32_t ms){memset(arena+at-BASE,0,64);write32(at+8,ms);write32(at+12,tls);write32(at+20,code);}
static DWORD WINAPI native_proc(void *parameter){return (DWORD)(uintptr_t)parameter;}
static DWORD WINAPI native_exit(void *parameter){ExitThread((DWORD)(uintptr_t)parameter);return 0;}
static void strict_case(const char *mode,uint32_t tls){
    struct fnv_cpu_fault fault;uint32_t args[6]={0};
    if(!strcmp(mode,"--strict-flags")){args[2]=0x401c00;args[4]=0x10000;assert(execute(CREATE,args,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"flags pending"));}
    else if(!strcmp(mode,"--strict-disabled")){args[2]=0x401c00;assert(execute(CREATE,args,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"provider unavailable"));}
    else if(!strcmp(mode,"--strict-static-tls")){args[2]=0x401c00;assert(execute(CREATE,args,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"static TLS initialization pending"));}
    else if(!strcmp(mode,"--strict-exit")){args[0]=123;assert(execute(EXIT,args,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"requires managed worker"));}
    else if(!strcmp(mode,"--strict-peer") || !strcmp(mode,"--strict-peer-suspended")){
        context(CTX,tls,1,UINT32_MAX);uint32_t h=spawn(0x401c00,CTX,!strcmp(mode,"--strict-peer-suspended")?4:0);assert(h);args[2]=0x401c00;args[4]=0x10000;
        assert(execute(CREATE,args,&fault)==FNV_CPU_GUEST_FAULT);assert(fnv_windows_thread_detach()==FNV_CPU_OK);settle();goto cleanup;
    }else {
        uint32_t h=spawn(!strcmp(mode,"--strict-bad-stack")?0x401df0:0x401fff,CTX,0);assert(h);args[0]=h;args[1]=5000;
        enum fnv_cpu_result result=execute(WAIT,args,&fault);assert(result==FNV_CPU_GUEST_FAULT || result==FNV_CPU_FAULTED);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);settle();
cleanup:
    assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    if(argc==3 && !strcmp(argv[2],"--strict-static-tls"))image.tls_rva=0x1000; /* metadata refusal, not a valid TLS directory */
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.kernel_handle_capacity=16;
    if(argc==2 || strcmp(argv[2],"--strict-disabled")){c.worker_address=BASE+0x40000;c.worker_bytes=0x30000;c.worker_stack_bytes=65536;c.worker_capacity=2;c.worker_host_stack_bytes=1024*1024;}
    struct fnv_windows_config bad=c;if(c.worker_capacity){bad.worker_host_stack_bytes=1;assert(fnv_windows_create(&bad,&process)==FNV_CPU_INVALID && !process);}
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    if(argc==3 && !strcmp(argv[2],"--strict-static-tls")){
        assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_FORMAT);
        assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==17);
    struct fnv_windows_thread_config t=main_config();assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t tls=call(TLS_ALLOC,0,0,0,0);assert(tls!=UINT32_MAX && call(TLS_SET,tls,0xeeee,0,0));if(argc==3){strict_case(argv[2],tls);return 0;}
    assert(call(SELF,0,0,0,0)==0xfffffffe && call(HID,0xfffffffe,0,0,0)==22 && call(CODE,0xfffffffe,OUTPUT,0,0) && read32(OUTPUT)==259);
    unsigned oracle=0;for(unsigned n=0;n<200;++n){uint32_t value=n*3571u+1;context(CTX,tls,value,0);
        SetLastError(0x4567);HANDLE native=CreateThread(NULL,0,n&1?native_exit:native_proc,(void *)(uintptr_t)value,CREATE_SUSPENDED,NULL);assert(native);DWORD error=GetLastError();
        call(SET_ERROR,0x4567,0,0,0);uint32_t h=spawn(n&1?0x401c80:0x401c00,CTX,4);assert(h);
        if(call(GET_ERROR,0,0,0,0)!=error){fprintf(stderr,"CreateThread LastError: guest %lu native %lu\n",(unsigned long)call(GET_ERROR,0,0,0,0),(unsigned long)error);abort();}
        uint32_t id=read32(OUTPUT);assert(id>=0x80000000 && call(HID,h,0,0,0)==id);
        DWORD code;assert(GetExitCodeThread(native,&code) && call(CODE,h,OUTPUT+4,0,0) && read32(OUTPUT+4)==code);
        assert(call(WAIT,h,0,0,0)==WaitForSingleObject(native,0));
        assert(call(RESUME,h,0,0,0)==ResumeThread(native));assert(WaitForSingleObject(native,5000)==0 && call(WAIT,h,5000,0,0)==0);
        assert(GetExitCodeThread(native,&code) && call(CODE,h,OUTPUT+4,0,0) && read32(OUTPUT+4)==code);
        assert(call(WAIT,h,0,0,0)==0 && read32(CTX)==id && read32(CTX+4)>=c.worker_address && read32(CTX+4)<c.worker_address+c.worker_bytes);
        assert(read32(CTX+24)==0 && read32(CTX+32)==0x12345678 && call(TLS_GET,tls,0,0,0)==0xeeee);
        assert(call(CLOSE,h,0,0,0) && CloseHandle(native));settle();oracle+=5;
    }
    context(CTX,tls,0x5555,20);context(CTX+64,tls,0x7777,0);
    for(unsigned n=0;n<25;++n){uint32_t h=spawn(0x401d80,CTX,0);assert(h && call(WAIT,h,5000,0,0)==0 && call(CLOSE,h,0,0,0));settle();assert(read32(CTX+64+32)==0x12345678);}
    /* Capacity cannot reclaim a live/suspended slot. Close does not terminate. */
    uint32_t h[2];for(unsigned n=0;n<2;++n){context(CTX+n*64,tls,n+1,30);h[n]=spawn(0x401c00,CTX+n*64,4);assert(h[n]);}
    assert(!spawn(0x401c00,CTX,0) && call(GET_ERROR,0,0,0,0)==8);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_BUSY && fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    for(unsigned n=0;n<2;++n)assert(call(RESUME,h[n],0,0,0)==1 && call(CLOSE,h[n],0,0,0));settle();
    for(unsigned n=0;n<32;++n){context(CTX,tls,n+1,1);uint32_t handle=spawn(0x401c00,CTX,0);assert(handle && call(CLOSE,handle,0,0,0));settle();}
    uint32_t mutex=call(MUTEX,0,0,0,0);assert(mutex);context(CTX,tls,77,0);write32(CTX+28,mutex);
    uint32_t handle=spawn(0x401d00,CTX,0);assert(handle && call(WAIT,handle,5000,0,0)==0 && call(CLOSE,handle,0,0,0));settle();
    assert(call(WAIT,mutex,0,0,0)==WAIT_ABANDONED && call(CLOSE,mutex,0,0,0));
    ULONGLONG before=GetTickCount64();call(SLEEP,25,0,0,0);assert(GetTickCount64()-before>=24);call(SLEEP,0,0,0,0);
    write32(OUTPUT,0x99887766);assert(!call(CODE,handle,OUTPUT,0,0) && call(GET_ERROR,0,0,0,0)==6 && read32(OUTPUT)==0x99887766);
    struct fnv_windows_handle_stats hs;assert(fnv_windows_handle_stats(process,&hs)==FNV_CPU_OK && !hs.live_handles && !hs.waiting_threads);
    struct fnv_windows_worker_stats ws=stats();assert(!ws.active && !ws.failures && ws.created==285 && ws.reaped+ws.finished==ws.created);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"managed_threads\":%llu,\"windows_oracle_comparisons\":%u,\"explicit_exit_threads\":100,\"nested_thread_rounds\":25,\"close_while_running\":34,\"suspend_resume\":true,\"tls_isolated\":true,\"abandoned_mutex\":true,\"native_engine_started\":false}\n",(unsigned long long)ws.created,oracle);return 0;
}
