/* Original USER32 display enumeration ABI, backend supplied by the platform. */
#include "fnv_sdk_runtime.h"
#include "windows_display.h"
#include "guest_memory.h"
#include <stdlib.h>
#include <string.h>
struct fnv_windows_display {struct fnv_cpu_runtime *cpu;struct fnv_display_backend backend;uint32_t token;};
static struct fnv_windows_display *active;
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static unsigned char *range(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,size;
        if(!fnv_vm_binding(vm,&data,&base,&size) || ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))p=NULL;}
    if(!p)recomp_unimpl("Display enumeration inaccessible guest buffer",at);
    return p;
}
static int terminated(const char *s,size_t n){return memchr(s,0,n)!=NULL;}
static void enumerate(void){
    uint32_t parent=fnv_cpu_argument_u32(0),index=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3);
    if(!active || active->cpu!=fnv_cpu_current()){recomp_unimpl("Display provider unavailable",out);return;}
    if(flags>1){recomp_unimpl("Display enumeration flags pending",flags);return;}
    char name[32];
    if(parent){unsigned i;
        for(i=0;i<32;i++){
            if((uint64_t)parent+i>UINT32_MAX){recomp_unimpl("Display enumeration parent address wraps",parent);return;}
            unsigned char *p=range(parent+i,1,0);if(!p)return;name[i]=(char)*p;if(!*p)break;
        }
        if(i==32){recomp_unimpl("Display enumeration parent name too long",parent);return;}
    }
    unsigned char *p=range(out,4,0);if(!p)return;uint32_t cb=rd(p),bytes=cb>424?424:cb;
    if(cb){p=range(out,cb,1);if(!p)return;}
    if(!active->backend.enumerate){recomp_unimpl("Display native enumeration backend unavailable",out);return;}
    struct fnv_display_device info={0};int result=active->backend.enumerate(active->backend.context,parent?name:NULL,index,flags,&info);
    if(result!=0 && result!=1){recomp_unimpl("Display native enumeration failed",index);return;}
    unsigned char record[424]={0};wr(record,cb);
    if(result){
        if(!info.name[0] || !terminated(info.name,32) || !terminated(info.description,128) || !terminated(info.id,128) || !terminated(info.key,128)){
            recomp_unimpl("Display backend record invalid",index);return;}
        /* Windows zeroes the writable prefix, then fills only complete fields. */
        if(cb>=36)memcpy(record+4,info.name,32);
        if(cb>=164)memcpy(record+36,info.description,128);
        if(cb>=168)wr(record+164,info.state_flags);
        if(cb>=296)memcpy(record+168,info.id,128);
        if(cb>=424)memcpy(record+296,info.key,128);
    }
    if(cb)memset(p,0,cb);
    if(bytes)memcpy(p,record,bytes);
    fnv_cpu_return_u32((uint32_t)result,16); /* Native ABI preserves LastError. */
}
static const struct fnv_cpu_export exports[]={{"user32.dll","EnumDisplayDevicesA",0,0,enumerate}};
const struct fnv_cpu_export *fnv_windows_display_exports(size_t *count){if(count)*count=1;return exports;}
enum fnv_cpu_result fnv_windows_display_create(struct fnv_windows_process *process,const struct fnv_display_backend *backend,struct fnv_windows_display **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!process || !backend)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_windows_display *d=calloc(1,sizeof(*d));if(!d)return FNV_CPU_NO_MEMORY;
    d->cpu=fnv_windows_runtime(process);d->backend=*backend;fnv_cpu_function fn=enumerate;
    enum fnv_cpu_result r=fnv_cpu_register_native(d->cpu,&fn,1,&d->token);if(r){free(d);return r;}
    active=d;*out=d;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_display_destroy(struct fnv_windows_display *d){
    if(!d || d!=active)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(d->cpu,d->token,1);if(r)return r;
    active=NULL;free(d);return FNV_CPU_OK;
}
