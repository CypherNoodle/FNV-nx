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
#endif
