#include "fnv_sdk_runtime.h"
#include "windows_memory.h"
#include "guest_memory.h"
#include <stdlib.h>
static struct fnv_guest_memory *manager(void) {
    struct fnv_guest_memory *v=fnv_windows_current_memory();
    if(!v){recomp_unimpl("Windows VM provider unavailable",0);abort();}return v;
}
static void finish(struct fnv_vm_result r,uint32_t args) {
    if(r.error==FNV_VM_CORRUPT){recomp_unimpl("Windows VM permission rollback failed",r.native_error);abort();}
    if(r.error)fnv_windows_set_last_error(r.error);
    fnv_cpu_return_u32(r.value,args);
}
static unsigned char *output_pointer(struct fnv_guest_memory *v,uint32_t address,uint32_t bytes) {
    unsigned char *backing;uint32_t base,size;fnv_vm_binding(v,&backing,&base,&size);
    uint64_t end=(uint64_t)address+bytes;
    if((uint64_t)address<(uint64_t)base+size && end>base &&
       !fnv_vm_access(v,address,bytes,1))return NULL;
    return fnv_cpu_memory(address,bytes);
}
static void write32(unsigned char *p,uint32_t value) { for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(8*i)); }
static void api_allocate(void) {
    uint32_t at=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1);
    uint32_t flags=fnv_cpu_argument_u32(2),protection=fnv_cpu_argument_u32(3);
    finish(fnv_vm_allocate(manager(),at,bytes,flags,protection),16);
}
static void api_free(void) {
    uint32_t at=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);
    finish(fnv_vm_free(manager(),at,bytes,flags),12);
}
static void api_query(void) {
    uint32_t at=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2);
    struct fnv_guest_memory *v=manager();struct fnv_vm_information info;
    if(bytes<28){finish((struct fnv_vm_result){0,87,0},12);return;}
    unsigned char *destination=output_pointer(v,out,28);
    if(!destination){finish((struct fnv_vm_result){0,998,0},12);return;}
    unsigned char *backing;uint32_t base,size;fnv_vm_binding(v,&backing,&base,&size);
    if(at<base || (uint64_t)at>=(uint64_t)base+size){recomp_unimpl("VirtualQuery outside managed data pool",at);abort();}
    struct fnv_vm_result r=fnv_vm_query(v,at,&info);
    if(!r.error) {
        uint32_t values[]={info.base,info.allocation_base,info.allocation_protect,info.size,info.state,info.protect,info.type};
        for(unsigned i=0;i<7;i++)write32(destination+i*4,values[i]);
    }
    finish(r,12);
}
static void api_protect(void) {
    uint32_t at=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1);
    uint32_t protection=fnv_cpu_argument_u32(2),out=fnv_cpu_argument_u32(3);
    struct fnv_guest_memory *v=manager();unsigned char *destination=output_pointer(v,out,4);
    if(!destination){finish((struct fnv_vm_result){0,998,0},16);return;}
    uint64_t low=(uint64_t)at&~UINT64_C(4095),high=((uint64_t)at+bytes+4095)&~UINT64_C(4095);
    /* Avoid changing the permissions of the result pointer before writing it. */
    if(bytes && (uint64_t)out<high && (uint64_t)out+4>low){finish((struct fnv_vm_result){0,50,0},16);return;}
    uint32_t old;struct fnv_vm_result r=fnv_vm_protect(v,at,bytes,protection,&old);
    if(!r.error)write32(destination,old);
    finish(r,16);
}
static const struct fnv_cpu_export exports[FNV_WINDOWS_MEMORY_EXPORTS]={
    {"kernel32.dll","VirtualAlloc",0,0,api_allocate},
    {"kernel32.dll","VirtualFree",0,0,api_free},
    {"kernel32.dll","VirtualQuery",0,0,api_query},
    {"kernel32.dll","VirtualProtect",0,0,api_protect},
};
const struct fnv_cpu_export *fnv_windows_memory_exports(size_t *count) {
    if(count)*count=FNV_WINDOWS_MEMORY_EXPORTS;
    return exports;
}
