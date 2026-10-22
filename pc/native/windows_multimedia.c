#include "fnv_sdk_runtime.h"
#include "windows_thread.h"
#include "windows_multimedia.h"
static void time_get_time(void){fnv_cpu_return_u32(fnv_windows_uptime_ms(),0);}
static const struct fnv_cpu_export exports[]={{"winmm.dll","timeGetTime",0,0,time_get_time}};
const struct fnv_cpu_export *fnv_windows_multimedia_exports(size_t *count){if(count)*count=1;return exports;}
