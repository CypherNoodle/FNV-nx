#include "fnv_sdk_runtime.h"
#include "windows_strings.h"
#include "windows_thread.h"
#include "windows_system.h"
#include "guest_memory.h"
#include <string.h>
#define STRING_LIMIT 1048576u
static unsigned char *page(uint32_t at,uint32_t n,int write){
    if(!at)return NULL;
    unsigned char *p=fnv_cpu_try_memory(at,n);if(!p)return NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&data,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))return NULL;}
    return p;
}
static int length_at(uint32_t src,uint64_t end,uint32_t *length){
    *length=0;
    if(!src)return 0;
    while(*length<STRING_LIMIT){
        uint64_t at=(uint64_t)src+*length;if(at>=end || at>UINT32_MAX)return 0;
        uint32_t n=4096-((uint32_t)at&4095);if(n>STRING_LIMIT-*length)n=STRING_LIMIT-*length;if(n>end-at)n=(uint32_t)(end-at);
        unsigned char *p=page((uint32_t)at,n,0);if(!p)return 0;
        unsigned char *nul=memchr(p,0,n);if(nul){*length+=(uint32_t)(nul-p)+1;return 1;}*length+=n;
        if(*length==STRING_LIMIT){recomp_unimpl("Windows byte string scan profile exceeded",src);return 0;}
    }
    return 0;
}
static void string_copy(int append){
    uint32_t dst=fnv_cpu_argument_u32(0),src=fnv_cpu_argument_u32(1),base,bytes;fnv_windows_current_address_bounds(&base,&bytes);
    uint64_t end=(uint64_t)base+bytes;uint32_t length=0,prefix=0;
    if(!dst || dst<base || src<base || !length_at(src,end,&length) || (append && !length_at(dst,end,&prefix))){fnv_cpu_return_u32(0,8);return;}
    if(append)prefix--;
    uint64_t at=(uint64_t)dst+prefix;if(at+length>end || at>UINT32_MAX){fnv_cpu_return_u32(0,8);return;}
    if((uint64_t)dst<(uint64_t)src+length && (uint64_t)src<at+length){recomp_unimpl("Windows byte string overlapping buffers undefined",dst);return;}
    for(uint32_t done=0;done<length;){uint32_t n=4096-(((uint32_t)at+done)&4095);if(n>length-done)n=length-done;
        if(!page((uint32_t)at+done,n,1)){fnv_cpu_return_u32(0,8);return;}done+=n;}
    unsigned char *d=fnv_cpu_try_memory((uint32_t)at,length),*s=fnv_cpu_try_memory(src,length);
    if(!d || !s){fnv_cpu_return_u32(0,8);return;}
    memcpy(d,s,length);fnv_cpu_return_u32(dst,8);
}
static void api_lstrcpy(void){string_copy(0);}static void api_lstrcat(void){string_copy(1);}
static const struct fnv_cpu_export exports[]={{"kernel32.dll","lstrcpyA",0,0,api_lstrcpy},{"kernel32.dll","lstrcatA",0,0,api_lstrcat}};
const struct fnv_cpu_export *fnv_windows_strings_exports(size_t *n){if(n)*n=2;return exports;}
