/* Force-include when compiling generated C AND the PC runtime. The external
 * SDK stays unchanged. A trace is per guest thread, just like its registers. */
#ifndef FNV_SDK_RUNTIME_H
#define FNV_SDK_RUNTIME_H
#ifndef RECOMP_GENERATED_CODE
#define RECOMP_GENERATED_CODE
#endif
#include "recomp_types.h"
extern RECOMP_TLS volatile uint32_t fnv_cpu_icall_trace[ICALL_TRACE_SIZE];
extern RECOMP_TLS volatile uint32_t fnv_cpu_icall_trace_idx;
extern RECOMP_TLS volatile uint64_t fnv_cpu_icall_count;
#define g_icall_trace fnv_cpu_icall_trace
#define g_icall_trace_idx fnv_cpu_icall_trace_idx
#define g_icall_count fnv_cpu_icall_count
#ifdef FNV_CPU_EXTENDED_X87
#include "x87_stack.h"
#ifdef FNV_CPU_X87_QUAD_MATH
#include "x87_transcendental_candidate.h"
#ifdef FNV_CPU_X87_QUAD_TRIG
#include "x87_trig_candidate.h"
#endif
#endif
extern RECOMP_TLS struct fnv_x87_stack fnv_x87_state;
extern RECOMP_TLS struct fnv_x87_environment fnv_x87_environment_state;
/* The SDK's unused function-local FP macros may remain in a function prelude.
 * Expanding one, or referring to an old FP global, must fail to compile. Old
 * precompiled FP code also cannot link: extended runtime defines none of the
 * legacy FP globals. The external SDK itself is never edited. */
#define g_fp_stack FNV_FORBIDDEN_LEGACY_X87_STACK
#define g_fp_top FNV_FORBIDDEN_LEGACY_X87_TOP
#define g_fp_cmp FNV_FORBIDDEN_LEGACY_X87_COMPARE
#define g_fp_control_word FNV_FORBIDDEN_LEGACY_X87_CONTROL
#define g_fp_cc FNV_FORBIDDEN_LEGACY_X87_STATUS
#endif
#endif
