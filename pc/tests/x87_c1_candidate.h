#ifndef FNV_TEST_X87_C1_CANDIDATE_H
#define FNV_TEST_X87_C1_CANDIDATE_H
#include "x87_extended.h"
/* Isolated arithmetic experiment; not selected by the production core.
 * Callers must replace C1 in their status, rather than OR a previous C1.
 * Other exception/control/operand limitations remain those of the base API. */
int fnv_x87_c1_candidate(enum fnv_x87_operation,uint16_t,
    struct fnv_x87_extended,struct fnv_x87_extended,
    struct fnv_x87_extended *,uint16_t *);
#endif
