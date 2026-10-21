/* Original local-console USER32 bridge, independent of the host desktop. */
#include "fnv_sdk_runtime.h"
#include "windows_user32.h"
#include "windows_thread.h"
static void api_system_metrics(void){
    uint32_t index=fnv_cpu_argument_u32(0);
    (void)fnv_windows_current_address_space_bytes(); /* Require attached guest thread. */
    /* SM_REMOTESESSION: the Switch guest has a local console, no Terminal
     * Services client. Do not infer unrelated metrics from this boolean. */
    if(index!=0x1000u){recomp_unimpl("Windows system metric pending",index);return;}
    fnv_cpu_return_u32(0,4); /* Preserve LastError, including when the metric is zero. */
}
static const struct fnv_cpu_export exports[]={
    {"user32.dll","GetSystemMetrics",0,0,api_system_metrics}
};
const struct fnv_cpu_export *fnv_windows_user32_exports(size_t *count){
    if(count)*count=sizeof(exports)/sizeof(*exports);
    return exports;
}
