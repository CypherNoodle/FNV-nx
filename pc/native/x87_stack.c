#include "x87_stack.h"

static int canonical(struct fnv_x87_extended value) {
    unsigned exponent=value.sign_exponent&0x7fff;
    if(!exponent)return !value.significand; /* Denormal/pseudo operands need DE handling. */
    if(!(value.significand>>63))return 0; /* Unsupported unnormal/pseudo encodings. */
    if(exponent==0x7fff && (value.significand&UINT64_C(0x7fffffffffffffff)) &&
       !(value.significand&UINT64_C(0x4000000000000000)))return 0; /* sNaN FLD semantics pending. */
    return 1;
}
static void top_status(struct fnv_x87_stack *s) {
    s->status=(uint16_t)((s->status&~0x3800u)|((unsigned)s->top<<11));
}
void fnv_x87_stack_init(struct fnv_x87_stack *s) {
    if(s)*s=(struct fnv_x87_stack){.control=0x037f};
}
enum fnv_x87_stack_result fnv_x87_stack_load(struct fnv_x87_stack *s,const unsigned char bytes[10]) {
    if(!s || !bytes || s->top>7)return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    struct fnv_x87_extended value=fnv_x87_unpack(bytes);
    if(!canonical(value))return FNV_X87_STACK_UNSUPPORTED;
    unsigned next=(s->top+7u)&7u;
    if(s->occupied&(1u<<next))return FNV_X87_STACK_FULL;
    s->registers[next]=value;s->top=(uint8_t)next;s->occupied|=(uint8_t)(1u<<next);
    s->status&=(uint16_t)~0x200u;top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_store_pop(struct fnv_x87_stack *s,unsigned char bytes[10]) {
    if(!s || !bytes || s->top>7)return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    fnv_x87_pack(bytes,s->registers[s->top]);
    s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);
    s->status&=(uint16_t)~0x200u;top_status(s);return FNV_X87_STACK_OK;
}
static enum fnv_x87_stack_result calculate(struct fnv_x87_stack *s,enum fnv_x87_operation op,int pop) {
    if(!s || s->top>7 || op<FNV_X87_ADD || op>FNV_X87_SQRT ||
       (pop && op==FNV_X87_SQRT) || (!pop && op!=FNV_X87_SQRT))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    unsigned dest=pop ? (s->top+1u)&7u : s->top;
    if(!(s->occupied&(1u<<s->top)) || !(s->occupied&(1u<<dest)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended a=s->registers[dest],b=s->registers[s->top],result;
    if(!canonical(a) || !canonical(b))return FNV_X87_STACK_UNSUPPORTED;
    uint16_t flags;
    if(!fnv_x87_calculate(op,s->control,a,b,&result,&flags))return FNV_X87_STACK_INVALID;
    s->status|=flags;
    s->registers[dest]=result;
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)dest;}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_binary_pop(struct fnv_x87_stack *s,enum fnv_x87_operation op) {
    return calculate(s,op,1);
}
enum fnv_x87_stack_result fnv_x87_stack_sqrt(struct fnv_x87_stack *s) {
    return calculate(s,FNV_X87_SQRT,0);
}
enum fnv_x87_stack_result fnv_x87_stack_load_binary(struct fnv_x87_stack *s,const unsigned char *bytes,unsigned width) {
    if(!s || !bytes || s->top>7 || (width!=4 && width!=8))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    unsigned next=(s->top+7u)&7u;
    if(s->occupied&(1u<<next))return FNV_X87_STACK_FULL;
    uint64_t bits=0;for(unsigned i=0;i<width;i++)bits|=(uint64_t)bytes[i]<<(i*8);
    struct fnv_x87_extended value;uint16_t flags;
    if(!fnv_x87_from_binary(width,bits,&value,&flags))return FNV_X87_STACK_INVALID;
    s->registers[next]=value;s->top=(uint8_t)next;s->occupied|=(uint8_t)(1u<<next);
    s->status=(uint16_t)((s->status&~0x200u)|flags);top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_store_binary(struct fnv_x87_stack *s,unsigned char *bytes,unsigned width,int pop) {
    if(!s || !bytes || s->top>7 || (width!=4 && width!=8) || (pop!=0 && pop!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    uint64_t bits;uint16_t flags;
    if(!fnv_x87_to_binary(width,s->control,s->registers[s->top],&bits,&flags))return FNV_X87_STACK_INVALID;
    for(unsigned i=0;i<width;i++)bytes[i]=(unsigned char)(bits>>(i*8));
    s->status|=flags;
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
