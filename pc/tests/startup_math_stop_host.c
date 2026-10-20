/* Diagnostic-only host functions. Target ARM64 math is never guessed here. */
#include "fnv_sdk_runtime.h"
#include "cpu_runtime.h"
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
enum fnv_x87_stack_result fnv_x87_transcendental_candidate(struct fnv_x87_stack *s,unsigned op){
    (void)s;(void)op;
    recomp_unimpl("Target ARM64 x87 math reached in host CRT diagnostic",0);
    return FNV_X87_STACK_UNSUPPORTED;
}
enum fnv_x87_stack_result fnv_x87_trig_candidate(struct fnv_x87_stack *s,unsigned op){
    (void)s;(void)op;
    recomp_unimpl("Target ARM64 x87 trig reached in host CRT diagnostic",0);
    return FNV_X87_STACK_UNSUPPORTED;
}
