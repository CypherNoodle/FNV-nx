#ifndef FNV_X87_STACK_H
#define FNV_X87_STACK_H
#include "x87_extended.h"
/* Independent state for one guest thread. No binary64 projection is stored. */
struct fnv_x87_stack {
    struct fnv_x87_extended registers[8];
    uint16_t control,status;
    uint8_t top,occupied;
};
enum fnv_x87_stack_result {
    FNV_X87_STACK_OK, FNV_X87_STACK_INVALID, FNV_X87_STACK_EMPTY,
    FNV_X87_STACK_FULL, FNV_X87_STACK_UNSUPPORTED
};
void fnv_x87_stack_init(struct fnv_x87_stack *state);
enum fnv_x87_stack_result fnv_x87_stack_load(struct fnv_x87_stack *state,const unsigned char bytes[10]);
enum fnv_x87_stack_result fnv_x87_stack_store_pop(struct fnv_x87_stack *state,unsigned char bytes[10]);
enum fnv_x87_stack_result fnv_x87_stack_binary_pop(struct fnv_x87_stack *state,enum fnv_x87_operation operation);
enum fnv_x87_stack_result fnv_x87_stack_sqrt(struct fnv_x87_stack *state);
enum fnv_x87_stack_result fnv_x87_stack_load_binary(struct fnv_x87_stack *state,const unsigned char *bytes,unsigned width);
enum fnv_x87_stack_result fnv_x87_stack_store_binary(struct fnv_x87_stack *state,unsigned char *bytes,unsigned width,int pop);
enum fnv_x87_stack_result fnv_x87_stack_duplicate(struct fnv_x87_stack *state,unsigned index);
enum fnv_x87_stack_result fnv_x87_stack_exchange(struct fnv_x87_stack *state,unsigned index);
enum fnv_x87_stack_result fnv_x87_stack_store_register(struct fnv_x87_stack *state,unsigned index,int pop);
enum fnv_x87_stack_result fnv_x87_stack_constant(struct fnv_x87_stack *state,int one);
enum fnv_x87_stack_result fnv_x87_stack_compare(struct fnv_x87_stack *state,unsigned index,int quiet,unsigned pops);
enum fnv_x87_stack_result fnv_x87_stack_control(struct fnv_x87_stack *state,const unsigned char bytes[2]);
void fnv_x87_stack_clear_exceptions(struct fnv_x87_stack *state);
enum fnv_x87_stack_result fnv_x87_stack_load_integer(struct fnv_x87_stack *state,const unsigned char *bytes,unsigned width);
enum fnv_x87_stack_result fnv_x87_stack_store_integer(struct fnv_x87_stack *state,unsigned char *bytes,unsigned width,int pop,int truncate);
enum fnv_x87_stack_result fnv_x87_stack_round_integer(struct fnv_x87_stack *state);
enum fnv_x87_stack_result fnv_x87_stack_binary_register(struct fnv_x87_stack *state,enum fnv_x87_operation operation,unsigned destination,unsigned source,int pop,int reverse);
enum fnv_x87_stack_result fnv_x87_stack_binary_memory(struct fnv_x87_stack *state,enum fnv_x87_operation operation,const unsigned char *bytes,unsigned width,int integer,int reverse);
enum fnv_x87_stack_result fnv_x87_stack_compare_memory(struct fnv_x87_stack *state,const unsigned char *bytes,unsigned width,int pop);
enum fnv_x87_stack_result fnv_x87_stack_test_zero(struct fnv_x87_stack *state);
enum fnv_x87_stack_result fnv_x87_stack_sign(struct fnv_x87_stack *state,int absolute);
/* Full/empty/unsupported are explicit precondition failures, not implemented
 * x87 stack-fault delivery. Only masked exceptions are supported by this
 * staging core; unmasked controls and pending ES are rejected without writes. */
#endif
