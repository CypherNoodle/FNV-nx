#ifndef FNV_HOST_WALLCLOCK_H
#define FNV_HOST_WALLCLOCK_H
#include <stdint.h>
#include "host_clock.h"
#if defined(_WIN32) && !defined(__SWITCH__)
#include <windows.h>
#else
#include <time.h>
#endif
/* UTC FILETIME, 100 ns since 1601. Never use this clock for deadlines.
 * Negative Unix seconds are valid back to the FILETIME epoch. */
static inline int fnv_host_utc_from_unix(int64_t seconds,long nanos,uint64_t *out){
    const uint64_t epoch=UINT64_C(11644473600),scale=UINT64_C(10000000);
    if(!out || seconds < -INT64_C(11644473600) || nanos<0 || nanos>=1000000000L)return 0;
    uint64_t whole=seconds<0?epoch-(uint64_t)(-(seconds+1))-1:epoch+(uint64_t)seconds;
    uint64_t fraction=(uint64_t)nanos/100;
    if(whole>(UINT64_MAX-fraction)/scale)return 0;
    *out=whole*scale+fraction;return 1;
}
static inline int fnv_host_utc_filetime(uint64_t *out){
#if defined(_WIN32) && !defined(__SWITCH__)
    FILETIME value;GetSystemTimeAsFileTime(&value);
    *out=(uint64_t)value.dwLowDateTime|((uint64_t)value.dwHighDateTime<<32);return 1;
#else
    struct timespec value;
    if(clock_gettime(CLOCK_REALTIME,&value))return 0;
    return fnv_host_utc_from_unix((int64_t)value.tv_sec,value.tv_nsec,out);
#endif
}
/* Windows has a separate, coarse uptime counter. Other hosts use their
 * monotonic boot counter; guest QPC's process-relative epoch is unrelated. */
static inline int fnv_host_uptime_ms(uint64_t *out){
#if defined(_WIN32) && !defined(__SWITCH__)
    *out=GetTickCount64();return 1;
#else
    uint64_t value;if(!fnv_host_now_ns(&value))return 0;*out=value/1000000;return 1;
#endif
}
#endif
