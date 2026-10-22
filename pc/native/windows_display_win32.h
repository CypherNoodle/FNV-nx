#ifndef FNV_WINDOWS_DISPLAY_WIN32_H
#define FNV_WINDOWS_DISPLAY_WIN32_H
/* Desktop diagnostic backend only. This is never used on Horizon. */
#ifdef _WIN32
#include <windows.h>
#include <string.h>
#include "windows_display.h"
#include "windows_user32.h"
static inline int fnv_display_enumerate_win32(void *context,const char *parent,uint32_t index,uint32_t flags,struct fnv_display_device *out){
    (void)context;DISPLAY_DEVICEA d={0};d.cb=sizeof(d);
    if(!EnumDisplayDevicesA(parent,index,&d,flags))return 0;
    memcpy(out->name,d.DeviceName,32);memcpy(out->description,d.DeviceString,128);out->state_flags=d.StateFlags;
    memcpy(out->id,d.DeviceID,128);memcpy(out->key,d.DeviceKey,128);return 1;
}
static inline int fnv_display_metrics_win32(void *context,uint32_t index,int32_t *out){
    (void)context;
    if(index!=0 && index!=1 && (index<76 || index>80))return 0;
    *out=(int32_t)GetSystemMetrics((int)index);return 1;
}
#endif
#endif
