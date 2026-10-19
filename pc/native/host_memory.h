#ifndef FNV_HOST_MEMORY_H
#define FNV_HOST_MEMORY_H
#include <stdint.h>
#if defined(__SWITCH__)
#include <switch/kernel/svc.h>
#include <switch/result.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif
struct fnv_host_memory_snapshot {uint64_t total,available;uint32_t load;};
/* Horizon exposes the memory assigned to this application, not a guessed
 * console-wide DRAM size. Other hosts query their actual physical memory. */
static inline int fnv_host_memory_snapshot(struct fnv_host_memory_snapshot *out) {
    uint64_t total,available;
#if defined(__SWITCH__)
    uint64_t used;
    if(R_FAILED(svcGetInfo(&total,InfoType_TotalMemorySize,CUR_PROCESS_HANDLE,0)) ||
       R_FAILED(svcGetInfo(&used,InfoType_UsedMemorySize,CUR_PROCESS_HANDLE,0)) || used>total)return 0;
    available=total-used;
#elif defined(_WIN32)
    MEMORYSTATUSEX value={0};value.dwLength=sizeof(value);
    if(!GlobalMemoryStatusEx(&value))return 0;
    total=value.ullTotalPhys;available=value.ullAvailPhys;
#elif defined(__linux__)
    struct sysinfo value;
    if(sysinfo(&value) || !value.mem_unit || (uint64_t)value.totalram>UINT64_MAX/value.mem_unit ||
       (uint64_t)value.freeram>UINT64_MAX/value.mem_unit)return 0;
    total=(uint64_t)value.totalram*value.mem_unit;available=(uint64_t)value.freeram*value.mem_unit;
#else
    (void)out;return 0;
#endif
#if defined(__SWITCH__) || defined(_WIN32) || defined(__linux__)
    if(!total || available>total || total>UINT64_MAX/100)return 0;
    *out=(struct fnv_host_memory_snapshot){total,available,(uint32_t)((total-available)*100/total)};
    return 1;
#endif
}
#endif
