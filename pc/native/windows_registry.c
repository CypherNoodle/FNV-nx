/* Original empty virtual registry for portable installations. */
#include "fnv_sdk_runtime.h"
#include "windows_registry.h"
#include "windows_thread.h"
#include "guest_memory.h"
static int root_key(uint32_t h){return h==0x80000000u || h==0x80000001u || h==0x80000002u || h==0x80000003u || h==0x80000005u;}
static unsigned char *checked(uint32_t at,uint32_t bytes,int write){
    if(!at)return NULL;
    unsigned char *p=fnv_cpu_try_memory(at,bytes);
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(vm,&backing,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))return NULL;}
    return p;
}
static void api_open(void){
    uint32_t root=fnv_cpu_argument_u32(0),name=fnv_cpu_argument_u32(1),options=fnv_cpu_argument_u32(2);
    uint32_t access=fnv_cpu_argument_u32(3),out=fnv_cpu_argument_u32(4);
    if(!root_key(root)){recomp_unimpl("Windows registry root/handle pending",root);return;}
    if(options || (access&~0x000f033fu) || (access&0x300u)==0x300u){
        recomp_unimpl("Windows registry options/access pending",options?options:access);return;}
    unsigned char *destination=checked(out,4,1);
    if(!destination){recomp_unimpl("Windows registry output unavailable",out);return;}
    int empty=!name,terminated=!name;
    if(name)for(uint32_t i=0;i<1024;i++){
        uint64_t pos=(uint64_t)name+i;unsigned char *p=pos<=UINT32_MAX?checked((uint32_t)pos,1,0):NULL;
        if(!p || *p>127){recomp_unimpl("Windows registry name unavailable/encoding pending",name);return;}
        if(!*p){empty=i==0;terminated=1;break;}
    }
    if(!terminated){recomp_unimpl("Windows registry name length pending",name);return;}
    /* All supported nonempty subkeys are absent, even if the PC host has them.
     * Windows clears the result on the tested missing-key path. */
    uint32_t handle=empty?root:0;
    for(unsigned i=0;i<4;i++)destination[i]=(unsigned char)(handle>>(8*i));
    fnv_cpu_return_u32(empty?0:2,20);
}
static void api_close(void){
    uint32_t h=fnv_cpu_argument_u32(0);
    if(!h){fnv_cpu_return_u32(6,4);return;} /* ERROR_INVALID_HANDLE, not an unimplemented operation. */
    if(!root_key(h)){recomp_unimpl("Windows registry close handle pending",h);return;}
    fnv_cpu_return_u32(0,4); /* Predefined keys are not closed. */
}
static const struct fnv_cpu_export exports[]={
    {"advapi32.dll","RegOpenKeyExA",0,0,api_open},
    {"advapi32.dll","RegCloseKey",0,0,api_close}
};
const struct fnv_cpu_export *fnv_windows_registry_exports(size_t *n){if(n)*n=2;return exports;}
