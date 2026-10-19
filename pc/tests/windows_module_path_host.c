#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define OUTPUT (BASE+0x9001u)
enum {PATH_A,PATH_W,GET_ERROR,SET_ERROR};
static const unsigned counts[]={3,3,0,1};
static unsigned char *arena;
static void put32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(i*8));}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *args,struct fnv_cpu_fault *fault,uint32_t *value){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    s.gpr[3]=0x3456789a;s.gpr[5]=0x456789ab;s.gpr[6]=0x56789abc;s.gpr[7]=0x6789abcd;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0x401500);
    for(unsigned i=0;i<counts[api];i++)put32(s.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+counts[api]*4);
        assert(s.gpr[3]==0x3456789a && s.gpr[5]==0x456789ab && s.gpr[6]==0x56789abc && s.gpr[7]==0x6789abcd);*value=s.gpr[0];}
    return r;
}
static uint32_t call(unsigned api,const uint32_t *args){struct fnv_cpu_fault fault;uint32_t value;assert(execute(api,args,&fault,&value)==FNV_CPU_OK);return value;}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    char snapshot[1024];DWORD length=GetModuleFileNameA(NULL,snapshot,sizeof(snapshot));assert(length && length<sizeof(snapshot));
    for(unsigned i=0;i<length;i++)assert((unsigned char)snapshot[i]<128);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;
    if(argc!=3 || strcmp(argv[2],"--strict-parameters")){c.parameters_address=BASE+0x30000;c.parameters_bytes=0x10000;c.command_line="FalloutNV.exe";c.image_path=snapshot;}
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);memset(snapshot,'!',sizeof(snapshot));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==4);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){
        uint32_t args[]={0,OUTPUT,2},value;const char *text="parameters unavailable";
        if(!strcmp(argv[2],"--strict-pointer")){args[1]=BASE+ARENA-1;text="pointer unavailable";}
        struct fnv_cpu_fault fault;assert(execute(PATH_W,args,&fault,&value)==FNV_CPU_GUEST_FAULT && strstr(fault.text,text));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    unsigned checks=0;
    for(unsigned api=PATH_A;api<=PATH_W;api++)for(unsigned handle=0;handle<3;handle++)for(unsigned size=0;size<=length+2;size++){
        unsigned char expected[4096];memset(expected,0x5a,sizeof(expected));memset(arena+OUTPUT-BASE-1,0x5a,sizeof(expected));
        uint32_t args[]={handle==0?0:handle==1?BASE:0x1234,OUTPUT,size},seed[]={0x3456};call(SET_ERROR,seed);SetLastError(0x3456);
        HMODULE native_handle=handle==0?NULL:handle==1?GetModuleHandleA(NULL):(HMODULE)(uintptr_t)0x1234;
        DWORD value=api==PATH_A?GetModuleFileNameA(native_handle,(char *)expected+1,size):GetModuleFileNameW(native_handle,(WCHAR *)(void *)(expected+1),size);
        DWORD error=GetLastError();uint32_t actual=call(api,args),actual_error=call(GET_ERROR,NULL);
        if(actual!=value || actual_error!=error || memcmp(expected,arena+OUTPUT-BASE-1,sizeof(expected)))
            fprintf(stderr,"api=%u handle=%u size=%u expected=%lu/%lu actual=%u/%u\n",api,handle,size,(unsigned long)value,(unsigned long)error,actual,actual_error);
        assert(actual==value && actual_error==error && !memcmp(expected,arena+OUTPUT-BASE-1,sizeof(expected)));checks++;
    }
    uint32_t null_output[]={0,0,0};assert(!call(PATH_A,null_output) && call(GET_ERROR,NULL)==122);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_path_comparisons\":%u,\"snapshot_owned\":true,\"A_W_truncation_and_last_error\":true,\"native_engine_started\":false}\n",checks);return 0;
}
