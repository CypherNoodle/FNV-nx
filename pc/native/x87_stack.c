#include "x87_stack.h"

static int canonical(struct fnv_x87_extended value) {
    unsigned exponent=value.sign_exponent&0x7fff;
    if(!exponent)return !value.significand; /* Denormal/pseudo operands need DE handling. */
    if(!(value.significand>>63))return 0; /* Unsupported unnormal/pseudo encodings. */
    if(exponent==0x7fff && (value.significand&UINT64_C(0x7fffffffffffffff)) &&
       !(value.significand&UINT64_C(0x4000000000000000)))return 0; /* sNaN register arithmetic pending. */
    return 1;
}
static void top_status(struct fnv_x87_stack *s) {
    s->status=(uint16_t)((s->status&~0x3800u)|((unsigned)s->top<<11));
}
void fnv_x87_stack_init(struct fnv_x87_stack *s) {
    if(s)*s=(struct fnv_x87_stack){.control=0x037f};
}
void fnv_x87_stack_reset(struct fnv_x87_stack *s) {
    if(s){s->control=0x037f;s->status=0;s->top=0;s->occupied=0;}
}
enum fnv_x87_stack_result fnv_x87_stack_load(struct fnv_x87_stack *s,const unsigned char bytes[10]) {
    if(!s || !bytes || s->top>7)return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    struct fnv_x87_extended value=fnv_x87_unpack(bytes);
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
    /* Raw ext80 transport now accepts encodings that numeric conversion has
     * not implemented. Never pass those through SoftFloat as valid numbers. */
    if(!canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    uint64_t bits;uint16_t flags;
    if(!fnv_x87_to_binary(width,s->control,s->registers[s->top],&bits,&flags))return FNV_X87_STACK_INVALID;
    for(unsigned i=0;i<width;i++)bytes[i]=(unsigned char)(bits>>(i*8));
    s->status|=flags;
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_duplicate(struct fnv_x87_stack *s,unsigned index) {
    if(!s || s->top>7 || index>7)return FNV_X87_STACK_INVALID;
    unsigned source=(s->top+index)&7u;
    if(!(s->occupied&(1u<<source)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[source]))return FNV_X87_STACK_UNSUPPORTED;
    unsigned char bytes[10];fnv_x87_pack(bytes,s->registers[source]);
    return fnv_x87_stack_load(s,bytes);
}
enum fnv_x87_stack_result fnv_x87_stack_exchange(struct fnv_x87_stack *s,unsigned index) {
    if(!s || s->top>7 || index>7)return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    unsigned source=(s->top+index)&7u;
    if(!(s->occupied&(1u<<source)) || !(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[source]) || !canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    struct fnv_x87_extended value=s->registers[source];s->registers[source]=s->registers[s->top];s->registers[s->top]=value;
    s->status&=(uint16_t)~0x200u;return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_store_register(struct fnv_x87_stack *s,unsigned index,int pop) {
    if(!s || s->top>7 || index>7 || (pop!=0 && pop!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    unsigned destination=(s->top+index)&7u;
    s->registers[destination]=s->registers[s->top];s->occupied|=(uint8_t)(1u<<destination);
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    s->status&=(uint16_t)~0x200u;top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_constant(struct fnv_x87_stack *s,int one) {
    if(one!=0 && one!=1)return FNV_X87_STACK_INVALID;
    struct fnv_x87_extended value={one ? UINT64_C(0x8000000000000000):0,one ? 0x3fff:0};
    unsigned char bytes[10];fnv_x87_pack(bytes,value);
    return fnv_x87_stack_load(s,bytes);
}
enum fnv_x87_stack_result fnv_x87_stack_compare(struct fnv_x87_stack *s,unsigned index,int quiet,unsigned pops) {
    if(!s || s->top>7 || index>7 || pops>2 || (pops==2 && index!=1) || (quiet!=0 && quiet!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    unsigned source=(s->top+index)&7u;
    if(!(s->occupied&(1u<<source)) || !(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[source]) || !canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    int order;uint16_t flags;
    if(!fnv_x87_compare(s->registers[s->top],s->registers[source],quiet,&order,&flags))return FNV_X87_STACK_INVALID;
    s->status=(uint16_t)((s->status&~0x4700u)|flags|(order==2 ? 0x4500u : order<0 ? 0x100u : order==0 ? 0x4000u : 0));
    for(unsigned i=0;i<pops;i++){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_control(struct fnv_x87_stack *s,const unsigned char bytes[2]) {
    if(!s || !bytes || s->top>7)return FNV_X87_STACK_INVALID;
    uint16_t value=(uint16_t)(bytes[0]|(uint16_t)bytes[1]<<8);
    if((value&0x7fu)!=0x7fu || (value&~0x1f7fu) || ((value>>8)&3u)==1 || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    s->control=value;return FNV_X87_STACK_OK;
}
void fnv_x87_stack_clear_exceptions(struct fnv_x87_stack *s) {
    if(s)s->status&=0x7f00u;
}
enum fnv_x87_stack_result fnv_x87_stack_load_integer(struct fnv_x87_stack *s,const unsigned char *bytes,unsigned width) {
    if(!s || !bytes || s->top>7 || (width!=2 && width!=4 && width!=8))return FNV_X87_STACK_INVALID;
    uint64_t bits=0;for(unsigned i=0;i<width;i++)bits|=(uint64_t)bytes[i]<<(i*8);
    struct fnv_x87_extended value;
    if(!fnv_x87_from_integer(width,bits,&value))return FNV_X87_STACK_INVALID;
    unsigned char memory[10];fnv_x87_pack(memory,value);return fnv_x87_stack_load(s,memory);
}
enum fnv_x87_stack_result fnv_x87_stack_store_integer(struct fnv_x87_stack *s,unsigned char *bytes,unsigned width,int pop,int truncate) {
    if(!s || !bytes || s->top>7 || (width!=2 && width!=4 && width!=8) || (pop!=0 && pop!=1) || (truncate!=0 && truncate!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    uint64_t bits;uint16_t flags;
    if(!fnv_x87_to_integer(width,s->control,s->registers[s->top],truncate,&bits,&flags))return FNV_X87_STACK_INVALID;
    for(unsigned i=0;i<width;i++)bytes[i]=(unsigned char)(bits>>(i*8));
    s->status|=flags;
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_round_integer(struct fnv_x87_stack *s) {
    if(!s || s->top>7)return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    struct fnv_x87_extended value;uint16_t flags;
    if(!fnv_x87_round_integer(s->control,s->registers[s->top],&value,&flags))return FNV_X87_STACK_INVALID;
    s->registers[s->top]=value;s->status|=flags;return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_binary_register(struct fnv_x87_stack *s,enum fnv_x87_operation op,unsigned destination,unsigned source,int pop,int reverse) {
    if(!s || s->top>7 || destination>7 || source>7 || op<FNV_X87_ADD || op>FNV_X87_DIV ||
       (pop!=0 && pop!=1) || (reverse!=0 && reverse!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    unsigned dst=(s->top+destination)&7u,src=(s->top+source)&7u;
    if(!(s->occupied&(1u<<dst)) || !(s->occupied&(1u<<src)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended a=s->registers[dst],b=s->registers[src],result;
    if(!canonical(a) || !canonical(b))return FNV_X87_STACK_UNSUPPORTED;
    if(reverse){struct fnv_x87_extended swap=a;a=b;b=swap;}
    uint16_t flags;
    if(!fnv_x87_calculate(op,s->control,a,b,&result,&flags))return FNV_X87_STACK_INVALID;
    s->registers[dst]=result;s->status|=flags;
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_binary_memory(struct fnv_x87_stack *s,enum fnv_x87_operation op,const unsigned char *bytes,unsigned width,int integer,int reverse) {
    if(!s || !bytes || s->top>7 || op<FNV_X87_ADD || op>FNV_X87_DIV ||
       (integer!=0 && integer!=1) || (reverse!=0 && reverse!=1) ||
       (integer ? (width!=2 && width!=4) : (width!=4 && width!=8)))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended a=s->registers[s->top],b,result;
    if(!canonical(a))return FNV_X87_STACK_UNSUPPORTED;
    uint64_t bits=0;for(unsigned i=0;i<width;i++)bits|=(uint64_t)bytes[i]<<(i*8);
    uint16_t loaded=0,flags;
    if(integer){if(!fnv_x87_from_integer(width,bits,&b))return FNV_X87_STACK_INVALID;}
    else {
        if(!fnv_x87_from_binary(width,bits,&b,&loaded))return FNV_X87_STACK_INVALID;
        /* Conversion quiets a signaling NaN. Arithmetic must still distinguish
         * it from a quiet NaN when selecting an operand's NaN payload. */
        if(loaded&1u)b.significand&=~UINT64_C(0x4000000000000000);
        /* x87 processes NaN operands before the denormal-operand exception.
         * A standalone FLD conversion would have reported DE already. */
        if((a.sign_exponent&0x7fff)==0x7fff && (a.significand&UINT64_C(0x7fffffffffffffff)))loaded&=(uint16_t)~2u;
    }
    if(reverse){struct fnv_x87_extended swap=a;a=b;b=swap;}
    if(!fnv_x87_calculate(op,s->control,a,b,&result,&flags))return FNV_X87_STACK_INVALID;
    s->registers[s->top]=result;s->status|=(uint16_t)(flags|loaded);return FNV_X87_STACK_OK;
}
static enum fnv_x87_stack_result compare_value(struct fnv_x87_stack *s,struct fnv_x87_extended value,uint16_t loaded,int pop) {
    if(!s || s->top>7 || (pop!=0 && pop!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended a=s->registers[s->top];
    if(!canonical(a))return FNV_X87_STACK_UNSUPPORTED;
    if((a.sign_exponent&0x7fff)==0x7fff && (a.significand&UINT64_C(0x7fffffffffffffff)))loaded&=(uint16_t)~2u;
    int order;uint16_t flags;
    if(!fnv_x87_compare(a,value,0,&order,&flags))return FNV_X87_STACK_INVALID;
    s->status=(uint16_t)((s->status&~0x4700u)|flags|loaded|(order==2 ? 0x4500u : order<0 ? 0x100u : order==0 ? 0x4000u : 0));
    if(pop){s->occupied&=(uint8_t)~(1u<<s->top);s->top=(uint8_t)((s->top+1u)&7u);}
    top_status(s);return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_compare_memory(struct fnv_x87_stack *s,const unsigned char *bytes,unsigned width,int pop) {
    if(!s || !bytes || (width!=4 && width!=8))return FNV_X87_STACK_INVALID;
    uint64_t bits=0;for(unsigned i=0;i<width;i++)bits|=(uint64_t)bytes[i]<<(i*8);
    struct fnv_x87_extended value;uint16_t flags;
    if(!fnv_x87_from_binary(width,bits,&value,&flags))return FNV_X87_STACK_INVALID;
    return compare_value(s,value,flags,pop);
}
enum fnv_x87_stack_result fnv_x87_stack_test_zero(struct fnv_x87_stack *s) {
    return compare_value(s,(struct fnv_x87_extended){0,0},0,0);
}
enum fnv_x87_stack_result fnv_x87_stack_sign(struct fnv_x87_stack *s,int absolute) {
    if(!s || s->top>7 || (absolute!=0 && absolute!=1))return FNV_X87_STACK_INVALID;
    if((s->control&0x3fu)!=0x3fu || (s->status&0x80u))return FNV_X87_STACK_UNSUPPORTED;
    if(!(s->occupied&(1u<<s->top)))return FNV_X87_STACK_EMPTY;
    if(!canonical(s->registers[s->top]))return FNV_X87_STACK_UNSUPPORTED;
    s->registers[s->top].sign_exponent=absolute ? s->registers[s->top].sign_exponent&0x7fff : s->registers[s->top].sign_exponent^0x8000;
    s->status&=(uint16_t)~0x200u;return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_math_constant(struct fnv_x87_stack *s,unsigned constant) {
    if(!s || constant>FNV_X87_LN2)return FNV_X87_STACK_INVALID;
    if(constant<2)return fnv_x87_stack_constant(s,(int)constant);
    /* Positive x87 ROM constants: lower adjacent payload and nearest choice.
     * PC does not narrow these loads; RC selects their 64-bit significand. */
    static const struct {uint64_t lower;uint16_t exponent;unsigned nearest_up;} constants[]={
        {UINT64_C(0xc90fdaa22168c234),0x4000,1},
        {UINT64_C(0xb8aa3b295c17f0bb),0x3fff,1},
        {UINT64_C(0xd49a784bcd1b8afe),0x4000,0},
        {UINT64_C(0x9a209a84fbcff798),0x3ffd,1},
        {UINT64_C(0xb17217f7d1cf79ab),0x3ffe,1}
    };
    unsigned index=constant-2,rc=(s->control>>10)&3u;
    struct fnv_x87_extended value={constants[index].lower+(rc==2 || (rc==0 && constants[index].nearest_up)),constants[index].exponent};
    unsigned char bytes[10];fnv_x87_pack(bytes,value);return fnv_x87_stack_load(s,bytes);
}
enum fnv_x87_stack_result fnv_x87_stack_wait(const struct fnv_x87_stack *s) {
    if(!s || s->top>7)return FNV_X87_STACK_INVALID;
    if((s->status&0x80u) || (s->status&~s->control&0x3fu))return FNV_X87_STACK_UNSUPPORTED;
    return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_examine(struct fnv_x87_stack *s) {
    enum fnv_x87_stack_result ready=fnv_x87_stack_wait(s);if(ready!=FNV_X87_STACK_OK)return ready;
    struct fnv_x87_extended value=s->registers[s->top];
    unsigned exponent=value.sign_exponent&0x7fff,significand_integer=(unsigned)(value.significand>>63);
    uint16_t classification;
    if(!(s->occupied&(1u<<s->top)))classification=0x4100;
    else if(!exponent)classification=value.significand ? 0x4400 : 0x4000;
    else if(!significand_integer)classification=0;
    else if(exponent!=0x7fff)classification=0x0400;
    else classification=(value.significand&UINT64_C(0x7fffffffffffffff)) ? 0x0100 : 0x0500;
    s->status=(uint16_t)((s->status&~0x4700u)|classification|((value.sign_exponent&0x8000u)?0x0200:0));
    return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_free(struct fnv_x87_stack *s,unsigned index) {
    if(!s || index>7)return FNV_X87_STACK_INVALID;
    enum fnv_x87_stack_result ready=fnv_x87_stack_wait(s);if(ready!=FNV_X87_STACK_OK)return ready;
    s->occupied&=(uint8_t)~(1u<<((s->top+index)&7u));return FNV_X87_STACK_OK;
}

static unsigned environment_u16(const unsigned char *p) {
    return (unsigned)p[0]|(unsigned)p[1]<<8;
}
static uint32_t environment_u32(const unsigned char *p) {
    return (uint32_t)environment_u16(p)|(uint32_t)environment_u16(p+2)<<16;
}
static void environment_put16(unsigned char *p,unsigned value) {
    p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);
}
static void environment_put32(unsigned char *p,uint32_t value) {
    environment_put16(p,value);environment_put16(p+2,value>>16);
}
static unsigned environment_tag(struct fnv_x87_extended value) {
    unsigned exponent=value.sign_exponent&0x7fffu;
    if(!exponent && !value.significand)return 1; /* Signed zero. */
    if(!exponent || exponent==0x7fff || !(value.significand>>63))return 2;
    return 0;
}
enum fnv_x87_stack_result fnv_x87_stack_save32(struct fnv_x87_stack *s,struct fnv_x87_environment *e,unsigned char *out,unsigned bytes,int full) {
    if(!s || !e || !out || s->top>7 || (full!=0 && full!=1) || bytes<(full?108u:28u))return FNV_X87_STACK_INVALID;
    if(e->opcode>0x7ff)return FNV_X87_STACK_UNSUPPORTED;
    unsigned char image[108];for(unsigned i=0;i<108;i++)image[i]=0;
    unsigned tags=0;
    for(unsigned i=0;i<8;i++)tags|=((s->occupied&(1u<<i))?environment_tag(s->registers[i]):3u)<<(2*i);
    environment_put16(image,s->control);environment_put16(image+2,0xffff);
    environment_put16(image+4,(s->status&~0x3800u)|((unsigned)s->top<<11));environment_put16(image+6,0xffff);
    environment_put16(image+8,tags);environment_put16(image+10,0xffff);
    environment_put32(image+12,e->instruction_pointer);environment_put16(image+16,e->code_selector);
    environment_put16(image+18,e->opcode);environment_put32(image+20,e->data_pointer);
    environment_put16(image+24,e->data_selector);environment_put16(image+26,0xffff);
    if(full)for(unsigned i=0;i<8;i++)fnv_x87_pack(image+28+10*i,s->registers[(s->top+i)&7u]);
    for(unsigned i=0;i<(full?108u:28u);i++)out[i]=image[i];
    if(full){fnv_x87_stack_reset(s);*e=(struct fnv_x87_environment){0};}
    else s->control|=0x3fu;
    return FNV_X87_STACK_OK;
}
enum fnv_x87_stack_result fnv_x87_stack_restore32(struct fnv_x87_stack *s,struct fnv_x87_environment *e,const unsigned char *in,unsigned bytes,int full) {
    if(!s || !e || !in || (full!=0 && full!=1) || bytes<(full?108u:28u))return FNV_X87_STACK_INVALID;
    enum fnv_x87_stack_result ready=fnv_x87_stack_wait(s);if(ready!=FNV_X87_STACK_OK)return ready;
    unsigned control=environment_u16(in),status=environment_u16(in+4),tags=environment_u16(in+8);
    /* This staging core accepts only its audited masked control subset.
     * ES/B handling for a restored unmasked exception is not approximated. */
    if((control&0x7fu)!=0x7fu || (control&~0x1f7fu) || ((control>>8)&3u)==1 || (status&0x8080u))return FNV_X87_STACK_UNSUPPORTED;
    struct fnv_x87_stack next=*s;
    struct fnv_x87_environment metadata={environment_u32(in+12),environment_u32(in+20),
        (uint16_t)environment_u16(in+16),(uint16_t)environment_u16(in+24),(uint16_t)(environment_u16(in+18)&0x7ffu)};
    next.control=(uint16_t)control;next.status=(uint16_t)status;next.top=(uint8_t)((status>>11)&7u);next.occupied=0;
    for(unsigned i=0;i<8;i++)if(((tags>>(i*2))&3u)!=3u)next.occupied|=(uint8_t)(1u<<i);
    /* Memory registers are logical ST(0)..ST(7), tags are physical R0..R7. */
    if(full)for(unsigned i=0;i<8;i++)next.registers[(next.top+i)&7u]=fnv_x87_unpack(in+28+10*i);
    *s=next;*e=metadata;return FNV_X87_STACK_OK;
}
