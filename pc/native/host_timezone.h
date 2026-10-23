#ifndef FNV_HOST_TIMEZONE_H
#define FNV_HOST_TIMEZONE_H
#include "timezone_rule.h"
#include <stdlib.h>
static inline int fnv_host_timezone(unsigned char output[172],uint32_t *id,uint32_t *error){
#if defined(__SWITCH__)
    u64 now;TimeLocationName location;TimeZoneRule *rule=malloc(sizeof(*rule));
    if(!rule){*error=8;return 0;}
    int ok=R_SUCCEEDED(timeGetCurrentTime(TimeType_Default,&now)) && now<=INT64_MAX &&
        R_SUCCEEDED(timeGetDeviceLocationName(&location)) && R_SUCCEEDED(timeLoadTimeZoneRule(&location,rule)) &&
        fnv_timezone_rule(rule->data,sizeof(rule->data),(int64_t)now,output,id);
    free(rule);if(!ok)*error=50;return ok;
#elif defined(_WIN32)
    TIME_ZONE_INFORMATION zone={0};DWORD state=GetTimeZoneInformation(&zone);
    if(state==TIME_ZONE_ID_INVALID){*error=GetLastError();return 0;}
    unsigned char result[172]={0};fnv_tz_put32(result,(uint32_t)zone.Bias);
    for(unsigned i=0;i<32;++i){fnv_tz_put16(result+4+i*2,zone.StandardName[i]);fnv_tz_put16(result+88+i*2,zone.DaylightName[i]);}
    const SYSTEMTIME *dates[]={&zone.StandardDate,&zone.DaylightDate};
    for(unsigned n=0;n<2;++n){const SYSTEMTIME *s=dates[n];const uint16_t fields[]={s->wYear,s->wMonth,s->wDayOfWeek,s->wDay,s->wHour,s->wMinute,s->wSecond,s->wMilliseconds};
        for(unsigned i=0;i<8;++i)fnv_tz_put16(result+(n?152:68)+i*2,fields[i]);}
    fnv_tz_put32(result+84,(uint32_t)zone.StandardBias);fnv_tz_put32(result+168,(uint32_t)zone.DaylightBias);
    memcpy(output,result,sizeof(result));*id=state;return 1;
#else
    (void)output;(void)id;*error=50;return 0; /* No POSIX transition-table provider. */
#endif
}
#endif
