#include "fnv_sdk_runtime.h"
#include "windows_filetime.h"
#include "windows_thread.h"
#include "guest_memory.h"
static uint64_t stamp(uint32_t at){
    unsigned char *p=fnv_cpu_try_memory(at,8);struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&data,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+8>base && !fnv_vm_access(vm,at,8,0)))p=NULL;}
    if(!at || !p){recomp_unimpl("Windows FILETIME input unavailable",at);return 0;}
    uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(i*8);return value;
}
static void compare(void){
    uint64_t a=stamp(fnv_cpu_argument_u32(0)),b=stamp(fnv_cpu_argument_u32(1));
    fnv_cpu_return_u32(a<b?UINT32_MAX:a>b?1:0,8);
}
static const struct fnv_cpu_export exports[]={{"kernel32.dll","CompareFileTime",0,0,compare}};
const struct fnv_cpu_export *fnv_windows_filetime_exports(size_t *n){if(n)*n=1;return exports;}
