/* Compile as an actual 32-bit Windows process: native ABI/layout oracle. */
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(void *)==4,"Run the x86 Windows oracle, not the x64 ABI");
_Static_assert(sizeof(SYSTEM_INFO)==36,"SYSTEM_INFO32 size");
_Static_assert(offsetof(SYSTEM_INFO,dwPageSize)==4,"page offset");
_Static_assert(offsetof(SYSTEM_INFO,lpMinimumApplicationAddress)==8,"minimum offset");
_Static_assert(offsetof(SYSTEM_INFO,lpMaximumApplicationAddress)==12,"maximum offset");
_Static_assert(offsetof(SYSTEM_INFO,dwActiveProcessorMask)==16,"mask offset");
_Static_assert(offsetof(SYSTEM_INFO,dwNumberOfProcessors)==20,"count offset");
_Static_assert(offsetof(SYSTEM_INFO,dwProcessorType)==24,"type offset");
_Static_assert(offsetof(SYSTEM_INFO,dwAllocationGranularity)==28,"granule offset");
_Static_assert(offsetof(SYSTEM_INFO,wProcessorLevel)==32,"level offset");
_Static_assert(offsetof(SYSTEM_INFO,wProcessorRevision)==34,"revision offset");
int main(void){SYSTEM_INFO s;GetSystemInfo(&s);
    printf("{\"bytes\":%u,\"architecture\":%u,\"page_bytes\":%lu,\"allocation_granule\":%lu}\n",
        (unsigned)sizeof(s),(unsigned)s.wProcessorArchitecture,(unsigned long)s.dwPageSize,(unsigned long)s.dwAllocationGranularity);
    return 0;
}
