#ifndef FNV_STARTUP_MATH_PIPE_H
#define FNV_STARTUP_MATH_PIPE_H
#include "x87_stack.h"
/* Desktop diagnostics only. Enable before workers; close after their exit. */
int fnv_startup_math_pipe_open(void);
void fnv_startup_math_pipe_close(void);
int fnv_startup_math_pipe_resolve(struct fnv_x87_stack *,unsigned,unsigned,enum fnv_x87_stack_result *);
#endif
