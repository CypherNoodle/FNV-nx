#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
enum {CREATE,RESUME,WAIT,CLOSE,SET,GET,LAST_ERROR,SET_ERROR};
static const unsigned counts[]={6,1,2,1,2,1,0,1};
static unsigned char *arena;
static void put(uint32_t at,uint32_t value){memcpy(arena+at-BASE,&value,4);}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put(s.gpr[4],0x401500);uint32_t args[]={a,b,c,d,e,f};
    for(unsigned i=0;i<counts[api];++i)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"priority API %u: %s\n",api,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+counts[api]*4);return s.gpr[0];
}
#define C(api,a,b) call(api,a,b,0,0,0,0)
static DWORD WINAPI native_proc(void *context){(void)context;return 0;}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;
    c.kernel_handle_capacity=8;c.worker_address=BASE+0x40000;c.worker_bytes=0x30000;c.worker_stack_bytes=65536;c.worker_capacity=2;c.worker_host_stack_bytes=1024*1024;
    struct fnv_windows_process *p;assert(fnv_windows_create(&c,&p)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;
    assert(fnv_windows_bind_imports(p,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==8);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(p,&t)==FNV_CPU_OK);int original=GetThreadPriority(GetCurrentThread());assert(original!=THREAD_PRIORITY_ERROR_RETURN);
    unsigned checks=0;
    for(unsigned i=0;i<100;++i){
        uint32_t h=call(CREATE,0,0,0x401c00,0,4,0);assert(h);
        HANDLE native=CreateThread(NULL,0,native_proc,NULL,CREATE_SUSPENDED,NULL);
        assert(native);
        for(int level=-2;level<=2;++level){
            C(SET_ERROR,0x8765,0);SetLastError(0x8765);
            assert(C(SET,h,(uint32_t)level)==(uint32_t)(SetThreadPriority(native,level)!=0));
            DWORD error=GetLastError();assert(C(LAST_ERROR,0,0)==error);
            assert((int32_t)C(GET,h,0)==GetThreadPriority(native));checks++;
            assert(C(SET,0xfffffffeu,(uint32_t)level));
            assert(GetThreadPriority(GetCurrentThread())==level && (int32_t)C(GET,0xfffffffeu,0)==level);checks++;
        }
        assert(SetThreadPriority(GetCurrentThread(),original));
        assert(C(RESUME,h,0)==1 && C(WAIT,h,5000)==WAIT_OBJECT_0 && C(CLOSE,h,0));
        assert(!C(SET,h,0) && C(LAST_ERROR,0,0)==6 && C(GET,h,0)==0x7fffffffu && C(LAST_ERROR,0,0)==6);
        assert(ResumeThread(native)==1 && WaitForSingleObject(native,5000)==0 && CloseHandle(native));
    }
    for(int level=-3;level<=3;level+=6)assert(!C(SET,0xfffffffeu,(uint32_t)level) && C(LAST_ERROR,0,0)==87);
    const int unsupported[]={-15,15,0x10000,0x20000};
    for(unsigned i=0;i<4;++i)assert(!C(SET,0xfffffffeu,(uint32_t)unsupported[i]) && C(LAST_ERROR,0,0)==50);
    assert(GetThreadPriority(GetCurrentThread())==original);
    struct fnv_windows_worker_stats stats;assert(fnv_windows_worker_stats(p,&stats)==FNV_CPU_OK && !stats.active && !stats.failures);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(p)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"priority_checks\":%u,\"managed_threads\":100,\"rejections\":6,\"native_engine_started\":false}\n",checks);return 0;
}
