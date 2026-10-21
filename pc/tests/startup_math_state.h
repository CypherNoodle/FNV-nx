#ifndef FNV_STARTUP_MATH_STATE_H
#define FNV_STARTUP_MATH_STATE_H
#include "x87_stack.h"
#define FNV_STARTUP_MATH_STATE_BYTES 86u
/* Physical register order, explicit little-endian bytes; no host padding. */
static inline void startup_math_pack(unsigned char out[FNV_STARTUP_MATH_STATE_BYTES],const struct fnv_x87_stack *s){
    for(unsigned i=0;i<8;i++)fnv_x87_pack(out+10*i,s->registers[i]);
    out[80]=(unsigned char)s->control;out[81]=(unsigned char)(s->control>>8);
    out[82]=(unsigned char)s->status;out[83]=(unsigned char)(s->status>>8);
    out[84]=s->top;out[85]=s->occupied;
}
static inline void startup_math_unpack(struct fnv_x87_stack *s,const unsigned char in[FNV_STARTUP_MATH_STATE_BYTES]){
    for(unsigned i=0;i<8;i++)s->registers[i]=fnv_x87_unpack(in+10*i);
    s->control=(uint16_t)(in[80]|(uint16_t)in[81]<<8);
    s->status=(uint16_t)(in[82]|(uint16_t)in[83]<<8);s->top=in[84];s->occupied=in[85];
}
#endif
