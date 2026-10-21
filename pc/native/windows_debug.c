#include "fnv_sdk_runtime.h"
#include "windows_debug.h"
#include "windows_thread.h"
#include "guest_memory.h"
static unsigned char *readable(uint32_t at,uint32_t bytes){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,size;
        if(!fnv_vm_binding(vm,&data,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,0)))p=NULL;
    }
    if(!p)recomp_unimpl("Windows thread-name record unavailable",at);
    return p;
}
static uint32_t word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void api_raise(void){
    uint32_t code=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),count=fnv_cpu_argument_u32(2),at=fnv_cpu_argument_u32(3);
    if(code!=0x406d1388 || flags || count!=4){recomp_unimpl("Windows guest exception dispatch pending",code);return;}
    unsigned char *p=readable(at,16);if(!p)return;
    uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)low;(void)high;
    if(word(p)!=0x1000 || word(p+12) || (word(p+8)!=id && word(p+8)!=UINT32_MAX)){
        recomp_unimpl("Windows thread-name record variant pending",at);return;}
    uint32_t text=word(p+4);char name[128];
    for(unsigned i=0;i<sizeof(name);i++){
        if((uint64_t)text+i>UINT32_MAX){recomp_unimpl("Windows thread-name address wraps",text);return;}
        unsigned char *c=readable(text+i,1);if(!c)return;
        if(*c>127){recomp_unimpl("Windows thread-name encoding pending",text);return;}
        name[i]=(char)*c;if(!*c){
            if(!fnv_windows_set_debug_name(name)){recomp_unimpl("Windows thread-name unavailable",text);return;}
            fnv_cpu_return_u32(0,16);return;
        }
    }
    recomp_unimpl("Windows thread-name length pending",text);
}
static const struct fnv_cpu_export exports[]={{"kernel32.dll","RaiseException",0,0,api_raise}};
const struct fnv_cpu_export *fnv_windows_debug_exports(size_t *count){if(count)*count=1;return exports;}
