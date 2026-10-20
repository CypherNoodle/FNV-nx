#include "x87_stack.h"
static int check(struct fnv_x87_extended value,int integer,unsigned width,
                 unsigned expected_flags,int pop,unsigned expected_top,unsigned expected_occupied) {
    struct fnv_x87_stack state;unsigned char raw[10],operand[4]={0};
    fnv_x87_stack_init(&state);fnv_x87_pack(raw,value);
    if(fnv_x87_stack_load(&state,raw)!=FNV_X87_STACK_OK)return 1;
    for(unsigned i=0;i<width;i++)operand[i]=(unsigned char)((unsigned)integer>>(i*8));
    if(fnv_x87_stack_compare_integer(&state,operand,width,pop)!=FNV_X87_STACK_OK)return 2;
    if((state.status&0x4500u)!=expected_flags || state.top!=expected_top || state.occupied!=expected_occupied)return 3;
    return 0;
}

int main(void) {
    const struct fnv_x87_extended plus_two={UINT64_C(0x8000000000000000),0x4000};
    const struct fnv_x87_extended minus_two={UINT64_C(0x8000000000000000),0xc000};
    const struct fnv_x87_extended zero={0,0};
    if(check(plus_two,3,2,0x0100,0,7,0x80) ||
       check(plus_two,2,2,0x4000,1,0,0) ||
       check(minus_two,-3,4,0,0,7,0x80) ||
       check(minus_two,-1,4,0x0100,1,0,0) ||
       check(zero,0,2,0x4000,0,7,0x80))return 1;
    struct fnv_x87_stack state,before;unsigned char raw[10],operand[4]={0};
    fnv_x87_stack_init(&state);fnv_x87_pack(raw,plus_two);
    if(fnv_x87_stack_load(&state,raw)!=FNV_X87_STACK_OK)return 2;
    before=state;
    if(fnv_x87_stack_compare_integer(&state,operand,8,0)!=FNV_X87_STACK_INVALID ||
       state.control!=before.control || state.status!=before.status || state.top!=before.top ||
       state.occupied!=before.occupied)return 3;
    for(unsigned i=0;i<8;i++)
        if(state.registers[i].significand!=before.registers[i].significand ||
           state.registers[i].sign_exponent!=before.registers[i].sign_exponent)return 4;
    return 0;
}
