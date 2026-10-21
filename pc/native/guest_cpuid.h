#ifndef FNV_GUEST_CPUID_H
#define FNV_GUEST_CPUID_H
#include <stdint.h>
/* Bounded virtual x86 contract: the observed extended-limit query advertises
 * no extended feature or brand leaves. It is independent of the ARM/PC host.
 * Other leaves remain unsupported until their feature contract is audited.
 * ECX is not a selector for leaf80000000, so all subleaf inputs agree. */
static inline int fnv_guest_cpuid_limits(void *context,uint32_t leaf,uint32_t subleaf,uint32_t out[4]){
    (void)context;(void)subleaf;
    if(!out || leaf!=UINT32_C(0x80000000))return 0;
    out[0]=UINT32_C(0x80000000);out[1]=out[2]=out[3]=0;
    return 1;
}
#endif
