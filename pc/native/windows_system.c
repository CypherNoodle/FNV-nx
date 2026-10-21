/* Original SYSTEM_INFO32 bridge. No native Windows structure passthrough. */
#include "fnv_sdk_runtime.h"
#include "windows_system.h"
#include "guest_memory.h"
#include <stdlib.h>
struct fnv_windows_system {struct fnv_cpu_runtime *cpu;uint32_t processors,token;};
/* Same single-process lifecycle restriction as the native module catalog.
 * Set up before guest threads attach; destroy only after all detach. */
static struct fnv_windows_system *active;
static void write32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void api_system_info(void){
    uint32_t address=fnv_cpu_argument_u32(0);
    if(!active || active->cpu!=fnv_cpu_current()){recomp_unimpl("Windows system provider unavailable",address);return;}
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    unsigned char *destination=fnv_cpu_try_memory(address,36);
    if(!destination || !address){recomp_unimpl("Windows SYSTEM_INFO output unavailable",address);return;}
    if(vm){unsigned char *backing;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&backing,&base,&bytes) ||
           ((uint64_t)address<(uint64_t)base+bytes && (uint64_t)address+36>base && !fnv_vm_access(vm,address,36,1))){
            recomp_unimpl("Windows SYSTEM_INFO output not writable",address);return;
        }
    }
    uint32_t base,bytes;fnv_windows_current_address_bounds(&base,&bytes);
    uint32_t processors=active->processors;
    /* Explicit virtual i686 family 6/revision 0. This describes the translated
     * guest ABI; it is not ARM64 host identification or a CPUID feature mask. */
    uint32_t values[]={0,4096,base,(uint32_t)((uint64_t)base+bytes-1),
        processors==32?UINT32_MAX:(1u<<processors)-1,processors,586,65536,6};
    for(unsigned i=0;i<9;i++)write32(destination+4*i,values[i]);
    fnv_cpu_return_u32(g_eax,4); /* void stdcall, guest LastError unchanged. */
}
static const struct fnv_cpu_export exports[]={{"kernel32.dll","GetSystemInfo",0,0,api_system_info}};
const struct fnv_cpu_export *fnv_windows_system_exports(size_t *count){if(count)*count=1;return exports;}
enum fnv_cpu_result fnv_windows_system_create(struct fnv_windows_process *process,uint32_t processors,struct fnv_windows_system **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!process || !processors || processors>32)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_windows_system *s=calloc(1,sizeof(*s));if(!s)return FNV_CPU_NO_MEMORY;
    s->cpu=fnv_windows_runtime(process);s->processors=processors;
    fnv_cpu_function function=api_system_info;
    enum fnv_cpu_result result=fnv_cpu_register_native(s->cpu,&function,1,&s->token);
    if(result!=FNV_CPU_OK){free(s);return result;}
    active=s;*out=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_system_destroy(struct fnv_windows_system *s){
    if(!s || active!=s)return FNV_CPU_INVALID;
    enum fnv_cpu_result result=fnv_cpu_unregister_native(s->cpu,s->token,1);
    if(result!=FNV_CPU_OK)return result;
    active=NULL;free(s);return FNV_CPU_OK;
}
