#ifndef FNV_X87_TRANSCENDENTAL_CANDIDATE_H
#define FNV_X87_TRANSCENDENTAL_CANDIDATE_H
#include "x87_stack.h"
enum fnv_x87_transcendental_operation {
    FNV_X87_CANDIDATE_F2XM1, FNV_X87_CANDIDATE_FPATAN, FNV_X87_CANDIDATE_FYL2X
};
/* Experimental ARM64 backend, selected only by the explicit v2 math profile.
 * Masked, canonical inputs only. Non-special finite exponents are bounded
 * to +/-1000 until extreme-range and underflow behavior has been verified.
 * Non-ARM64 hosts return UNSUPPORTED without changing guest state. */
enum fnv_x87_stack_result fnv_x87_transcendental_candidate(
    struct fnv_x87_stack *,unsigned operation);
#endif
