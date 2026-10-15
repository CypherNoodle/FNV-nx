/* Integer multiply helpers for the external x86 -> C pipeline. */
#ifndef FNV_MUL_FLAGS_H
#define FNV_MUL_FLAGS_H
#include "recomp_types.h"

/* bits is always 8, 16 or 32. Convert without implementation-defined narrow
 * signed casts; every signed product then fits in int64_t. */
static inline int64_t fnv_mul_signed(uint32_t value, unsigned bits) {
    uint64_t modulus = UINT64_C(1) << bits;
    uint64_t v = value & (modulus-1);
    return (v & (modulus >> 1)) ? (int64_t)v-(int64_t)modulus : (int64_t)v;
}
/* Ordinary x86 MUL/IMUL permits unaligned operands. Avoid typed unaligned
 * MEM16/MEM32 pointer accesses in the supplied SDK. */
static inline uint32_t fnv_mul_memory(uint32_t address, unsigned bytes) {
    uint32_t result = 0;
    for (unsigned i = 0; i < bytes; ++i)
        result |= (uint32_t)MEM8(address+i) << (8*i);
    return result;
}
#endif
