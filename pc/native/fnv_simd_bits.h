/* Bit-preserving SSE2 helpers for the externally supplied x86 recompiler.
 * No floating-point arithmetic, EFLAGS or MXCSR changes. */
#ifndef FNV_SIMD_BITS_H
#define FNV_SIMD_BITS_H
#include "recomp_types.h"
#include <stdlib.h>

/* MEM32 is a typed pointer in the supplied SDK. Byte accesses keep MOVDQU
 * and MOVLPD legal for arbitrary guest alignment without C alignment UB. */
static inline uint32_t fnv_simd_read32(uint32_t address) {
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= (uint32_t)MEM8(address+i) << (8*i);
    return value;
}
static inline void fnv_simd_write32(uint32_t address, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) MEM8(address+i) = (uint8_t)(value >> (8*i));
}
static inline RecompXmm fnv_xmm_loadu(uint32_t address) {
    RecompXmm result;
    for (unsigned i = 0; i < 4; ++i) result.u[i] = fnv_simd_read32(address+4*i);
    return result;
}
static inline void fnv_xmm_storeu(uint32_t address, RecompXmm value) {
    for (unsigned i = 0; i < 4; ++i) fnv_simd_write32(address+4*i, value.u[i]);
}
static inline RecompXmm fnv_xmm_loadlpd(RecompXmm old, uint32_t address) {
    old.u[0] = fnv_simd_read32(address); old.u[1] = fnv_simd_read32(address+4);
    return old;
}
static inline void fnv_xmm_storelpd(uint32_t address, RecompXmm value) {
    fnv_simd_write32(address, value.u[0]); fnv_simd_write32(address+4, value.u[1]);
}
static inline RecompXmm fnv_xmm_aligned_mem(uint32_t address, uint32_t guest_ip) {
    if (address & 15u) {
        /* Fail-stop instead of silently accepting an invalid legacy SSE2
         * operand. Guest #GP/SEH delivery still needs runtime integration. */
        RECOMP_UNIMPL("unaligned legacy SSE2 operand", guest_ip);
        abort();
    }
    return XMM_MEM(address);
}

static inline RecompXmm fnv_xmm_pshufd(RecompXmm source, uint32_t control) {
    RecompXmm result;
    for (unsigned i = 0; i < 4; ++i)
        result.u[i] = source.u[(control >> (2*i)) & 3u];
    return result;
}

static inline RecompXmm fnv_xmm_shufpd(RecompXmm a, RecompXmm b, uint32_t control) {
    RecompXmm result;
    result.q[0] = a.q[control & 1u];
    result.q[1] = b.q[(control >> 1) & 1u];
    return result;
}

static inline RecompXmm fnv_xmm_unpcklpd(RecompXmm a, RecompXmm b) {
    RecompXmm result;
    result.q[0] = a.q[0]; result.q[1] = b.q[0];
    return result;
}

static inline RecompXmm fnv_xmm_unpckhpd(RecompXmm a, RecompXmm b) {
    RecompXmm result;
    result.q[0] = a.q[1]; result.q[1] = b.q[1];
    return result;
}

/* Packed shifts use the entire low 64-bit count. Unlike scalar x86 shifts,
 * counts are not masked modulo the element width. Guard C's shift domain. */
static inline RecompXmm fnv_xmm_pslld(RecompXmm a, uint64_t count) {
    if (count >= 32) return XMM_ZERO();
    for (unsigned i = 0; i < 4; ++i) a.u[i] <<= count;
    return a;
}
static inline RecompXmm fnv_xmm_psrld(RecompXmm a, uint64_t count) {
    if (count >= 32) return XMM_ZERO();
    for (unsigned i = 0; i < 4; ++i) a.u[i] >>= count;
    return a;
}
static inline RecompXmm fnv_xmm_psllq(RecompXmm a, uint64_t count) {
    if (count >= 64) return XMM_ZERO();
    for (unsigned i = 0; i < 2; ++i) a.q[i] <<= count;
    return a;
}
static inline RecompXmm fnv_xmm_psrlq(RecompXmm a, uint64_t count) {
    if (count >= 64) return XMM_ZERO();
    for (unsigned i = 0; i < 2; ++i) a.q[i] >>= count;
    return a;
}
#endif
