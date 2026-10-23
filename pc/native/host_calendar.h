#ifndef FNV_HOST_CALENDAR_H
#define FNV_HOST_CALENDAR_H
#include "host_wallclock.h"
#if defined(__SWITCH__)
#include <switch/services/time.h>
#endif
/* Windows FileTimeToLocalFileTime uses the CURRENT timezone/DST offset,
 * not the historical offset at the timestamp being converted. */
static inline int fnv_host_current_utc_offset(int32_t *out){
#if defined(__SWITCH__)
    u64 now;TimeCalendarTime date;TimeCalendarAdditionalInfo info;
    if(R_FAILED(timeGetCurrentTime(TimeType_Default,&now)) ||
       R_FAILED(timeToCalendarTimeWithMyRule(now,&date,&info)))return 0;
    *out=info.offset;return 1; /* libnx initializes time during app startup. */
#elif defined(_WIN32)
    TIME_ZONE_INFORMATION zone;DWORD id=GetTimeZoneInformation(&zone);
    if(id==TIME_ZONE_ID_INVALID)return 0;
    int64_t bias=(int64_t)zone.Bias+(id==TIME_ZONE_ID_DAYLIGHT?zone.DaylightBias:id==TIME_ZONE_ID_STANDARD?zone.StandardBias:0);
    int64_t seconds=-bias*60;if(seconds<INT32_MIN || seconds>INT32_MAX)return 0;
    *out=(int32_t)seconds;return 1;
#else
    time_t now=time(NULL);struct tm local,utc;
    if(now==(time_t)-1 || !localtime_r(&now,&local) || !gmtime_r(&now,&utc))return 0;
    int days=local.tm_year==utc.tm_year?local.tm_yday-utc.tm_yday:local.tm_year>utc.tm_year?1:-1;
    if(days < -1 || days > 1)return 0;
    *out=days*86400+(local.tm_hour-utc.tm_hour)*3600+(local.tm_min-utc.tm_min)*60+local.tm_sec-utc.tm_sec;return 1;
#endif
}
static inline uint64_t fnv_calendar_local_filetime(uint64_t utc,int32_t offset){
    /* Unsigned modulo arithmetic matches Windows even before the epoch. */
    return utc+(uint64_t)((int64_t)offset*INT64_C(10000000));
}
static inline int fnv_calendar_leap(uint32_t year){return year%4==0 && (year%100!=0 || year%400==0);}
/* SYSTEMTIME: year, month, weekday (Sunday=0), day, hour, minute, second,
 * millisecond. Reject the signed FILETIME half without changing the output. */
static inline int fnv_calendar_systemtime(uint64_t value,uint16_t out[8]){
    if(value>>63)return 0;
    uint64_t ticks=value/10000,seconds=ticks/1000,days=seconds/86400;
    uint32_t year=1601+(uint32_t)(days/146097)*400,day=(uint32_t)(days%146097),month=1;
    for(;;){uint32_t length=365+(uint32_t)fnv_calendar_leap(year);if(day<length)break;day-=length;year++;}
    static const unsigned char months[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(;;){uint32_t length=months[month-1]+(month==2?(uint32_t)fnv_calendar_leap(year):0);if(day<length)break;day-=length;month++;}
    out[0]=(uint16_t)year;out[1]=(uint16_t)month;out[2]=(uint16_t)((days+1)%7);out[3]=(uint16_t)(day+1);
    out[4]=(uint16_t)(seconds/3600%24);out[5]=(uint16_t)(seconds/60%60);out[6]=(uint16_t)(seconds%60);out[7]=(uint16_t)(ticks%1000);return 1;
}
#endif
