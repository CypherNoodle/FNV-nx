#include "fnv_sdk_runtime.h"
#include "windows_com.h"
#include "windows_thread.h"
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
    {"ole32.dll","CoUninitialize",0,0,uninitialize}
};
const struct fnv_cpu_export *fnv_windows_com_exports(size_t *count){
    if(count)*count=sizeof(exports)/sizeof(*exports);
    return exports;
}
