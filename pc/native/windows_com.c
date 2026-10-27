#include "fnv_sdk_runtime.h"
#include "windows_com.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "windows_com_diag.h"
#include "windows_com_classes.h"
#include <string.h>
static void *activation_context;
static fnv_com_activation_trace activation_trace;
static void *class_context;
static fnv_com_class_factory class_factory;
int fnv_windows_com_set_factory(void *context,fnv_com_class_factory factory){
    if(factory && class_factory)return 0;
    class_context=context;class_factory=factory;return 1;
}
/* Setup only, before any guest thread attaches; caller owns callback lifetime. */
void fnv_windows_com_activation_trace(void *context,fnv_com_activation_trace trace){
    activation_context=context;activation_trace=trace;
}
static unsigned char *activation_memory(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,size;
        if(!fnv_vm_binding(vm,&data,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))p=NULL;
    }
    if(!p)recomp_unimpl("COM activation inaccessible guest memory",at);
    return p;
}
static void create_instance(void){
    uint32_t clsid=fnv_cpu_argument_u32(0),outer=fnv_cpu_argument_u32(1),
        context=fnv_cpu_argument_u32(2),iid=fnv_cpu_argument_u32(3),out=fnv_cpu_argument_u32(4);
    struct fnv_com_activation record={0};record.outer=outer;record.context=context;
    uint32_t hr=0x80070057u;
    if(out){
        unsigned char *p=activation_memory(out,4,1);if(!p)return;
        /* Read input GUIDs before clearing output, including aliased arguments. */
        unsigned char *c=activation_memory(clsid,16,0),*i=activation_memory(iid,16,0);
        if(!c || !i)return;
        memcpy(record.clsid,c,16);memcpy(record.iid,i,16);memset(p,0,4);
        if(!fnv_windows_com_initialization_count())hr=0x800401f0u;
        else {uint32_t object=0;
          if(class_factory && class_factory(class_context,record.clsid,outer,context,record.iid,&hr,&object)){
            if(!hr && !object){recomp_unimpl("COM factory returned null success",clsid);return;}
            if(hr && object){recomp_unimpl("COM factory returned object on failure",clsid);return;}
            p=activation_memory(out,4,1);if(!p)return;memcpy(p,&object,4);
          }else switch(context){
            /* Verified Windows results for an unregistered class. No registered
             * classes, objects, RPC servers or DirectShow graph exist here. */
            case 0:case 1:case 2:case 3:case 4:case 5:case 7:case 16:case 23:case 0x80000000u:
                hr=outer && (context&0x14u)?0x80040110u:0x80040154u;break;
            case 32:case UINT32_MAX:hr=0x80070057u;break;
            default:recomp_unimpl("COM activation context unsupported",context);return;
        }
        }
    }
    record.result=hr;if(activation_trace)activation_trace(activation_context,&record);
    fnv_cpu_return_u32(hr,20);
}
static void initialize(void){
    uint32_t reserved=fnv_cpu_argument_u32(0);
    fnv_cpu_return_u32(fnv_windows_com_initialize(reserved,2),4);
}
static void initialize_ex(void){
    uint32_t reserved=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1);
    fnv_cpu_return_u32(fnv_windows_com_initialize(reserved,flags),8);
}
static void uninitialize(void){
    fnv_windows_com_uninitialize();fnv_cpu_return_u32(0,0);
}
static const struct fnv_cpu_export exports[]={
    {"ole32.dll","CoInitialize",0,0,initialize},
    {"ole32.dll","CoInitializeEx",0,0,initialize_ex},
    {"ole32.dll","CoUninitialize",0,0,uninitialize},
    {"ole32.dll","CoCreateInstance",0,0,create_instance}
};
const struct fnv_cpu_export *fnv_windows_com_exports(size_t *count){
    if(count)*count=sizeof(exports)/sizeof(*exports);
    return exports;
}
