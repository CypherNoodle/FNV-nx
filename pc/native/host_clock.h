#ifndef FNV_HOST_CLOCK_H
#define FNV_HOST_CLOCK_H
#include <stdint.h>
#ifdef __SWITCH__
#include <switch/arm/counter.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif
/* Native monotonic time for waits, independent of guest RDTSC/game timers. */
static inline int fnv_host_now_ns(uint64_t *out){
    uint64_t ticks,hz;
#ifdef __SWITCH__
    ticks=armGetSystemTick();hz=armGetSystemTickFreq();
#elif defined(_WIN32)
    LARGE_INTEGER t,f;if(!QueryPerformanceCounter(&t) || !QueryPerformanceFrequency(&f) || t.QuadPart<0 || f.QuadPart<=0)return 0;
    ticks=(uint64_t)t.QuadPart;hz=(uint64_t)f.QuadPart;
#else
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t) || t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000 || (uint64_t)t.tv_sec>UINT64_MAX/1000000000)return 0;
    *out=(uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;return 1;
#endif
    if(!hz || hz>UINT32_MAX || ticks/hz>UINT64_MAX/1000000000)return 0;
    uint64_t seconds=(ticks/hz)*1000000000,fraction=(ticks%hz)*1000000000/hz;
    if(seconds>UINT64_MAX-fraction)return 0;
    *out=seconds+fraction;return 1;
}
#endif
