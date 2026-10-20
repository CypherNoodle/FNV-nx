/* Isolated research adapter, never linked into the game runtime.
 * The installed ld128 callers pass five arguments to a six-argument helper.
 * Supply an independently generated 2/pi table for the bounded probe only.
 */
#include <stdint.h>
#include "probe_two_over_pi.h"
extern int __real___kernel_rem_pio2(double *,double *,int,int,int,const int32_t *);
int __wrap___kernel_rem_pio2(double *x,double *y,int e0,int nx,int precision) {
    /* Only the finite, |x| < 2^64 research inputs are covered. These limits
     * leave ample table margin, including the helper's recomputation loop.
     * Do not use this bounded adapter for arbitrary binary128 arguments. */
    if(e0<0 || e0>40 || nx<1 || nx>5 || precision!=3)__builtin_trap();
    return __real___kernel_rem_pio2(x,y,e0,nx,precision,probe_two_over_pi);
}
