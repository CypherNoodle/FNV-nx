#ifndef FNV_X87_TRIG_CANDIDATE_H
#define FNV_X87_TRIG_CANDIDATE_H
#include "x87_stack.h"
enum fnv_x87_trig_operation {FNV_X87_TRIG_FSIN,FNV_X87_TRIG_FCOS,FNV_X87_TRIG_FPTAN,FNV_X87_TRIG_FSINCOS};
/* Explicit true-pi approximation model; not native x87 bit identity.
 * Finite |x| >= 2^63 sets C2 without pushing. In-range ordinary inputs below
 * exponent -1000, raw denormals, sNaNs and unmasked exceptions stop atomically. */
enum fnv_x87_stack_result fnv_x87_trig_candidate(struct fnv_x87_stack *,unsigned operation);
#endif
