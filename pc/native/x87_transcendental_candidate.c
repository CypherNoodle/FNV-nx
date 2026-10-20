/* Bounded experimental backend. Selected only by the explicit v2 math profile;
 * strict v1 generation never emits these calls. Not a native x87 bit clone. */
#include "x87_transcendental_candidate.h"
#include <float.h>
#if defined(FNV_CPU_X87_QUAD_MATH) && (!defined(__aarch64__) || LDBL_MANT_DIG!=113 || LDBL_MAX_EXP!=16384)
#error Selected quad model requires an ARM64 binary128 backend
#endif
#if defined(__aarch64__) && LDBL_MANT_DIG==113 && LDBL_MAX_EXP==16384
#include <math.h>
#include "x87_quad_bits.h"
static unsigned exponent(struct fnv_x87_extended x){return x.sign_exponent&0x7fff;}
static int zero(struct fnv_x87_extended x){return !exponent(x) && !x.significand;}
static int is_infinity(struct fnv_x87_extended x){return exponent(x)==0x7fff && x.significand==UINT64_C(0x8000000000000000);}
static int is_nan(struct fnv_x87_extended x){return exponent(x)==0x7fff && (x.significand&UINT64_C(0x7fffffffffffffff));}
static int canonical(struct fnv_x87_extended x){
    if(!exponent(x))return !x.significand;
    return (x.significand>>63) && (!is_nan(x) || (x.significand&UINT64_C(0x4000000000000000)));
}
static int bounded(struct fnv_x87_extended x){
    unsigned e=exponent(x);return !e || e==0x7fff || (e>=0x3fff-1000 && e<=0x3fff+1000);
}
static int one(struct fnv_x87_extended x){return exponent(x)==0x3fff && x.significand==UINT64_C(0x8000000000000000);}
static struct fnv_x87_extended special(unsigned sign,int inf){
    return (struct fnv_x87_extended){inf?UINT64_C(0x8000000000000000):0,(uint16_t)(sign|(inf?0x7fff:0))};
}
static union quad evaluate(unsigned op,struct fnv_x87_extended x,struct fnv_x87_extended y){
    uint64_t fpcr,fpsr;union quad q;
    __asm__ volatile("mrs %0,fpcr; mrs %1,fpsr; msr fpcr,xzr; msr fpsr,xzr":"=r"(fpcr),"=r"(fpsr)::"memory");
    if(op==FNV_X87_CANDIDATE_F2XM1)q.value=expm1l(load(x).value*0x1.62e42fefa39ef35793c7673007e6p-1L);
    else if(op==FNV_X87_CANDIDATE_FPATAN)q.value=atan2l(load(y).value,load(x).value);
    else q.value=log2l(load(x).value)*load(y).value;
    __asm__ volatile("msr fpcr,%0; msr fpsr,%1"::"r"(fpcr),"r"(fpsr):"memory");
    return q;
}
enum fnv_x87_stack_result fnv_x87_transcendental_candidate(struct fnv_x87_stack *s,unsigned op){
    if(!s || s->top>7 || op>FNV_X87_CANDIDATE_FYL2X)return FNV_X87_STACK_INVALID;
    if((s->control&0x3f)!=0x3f || ((s->control>>8)&3)==1 || (s->status&0x80))return FNV_X87_STACK_UNSUPPORTED;
    unsigned t=s->top,d=op?((t+1)&7):t;
    if(!(s->occupied&(1u<<t)) || !(s->occupied&(1u<<d)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended x=s->registers[t],y=s->registers[d],z;
    if(!canonical(x) || (op && !canonical(y)))return FNV_X87_STACK_UNSUPPORTED;
    uint16_t flags=0;unsigned rc=(s->control>>10)&3,ys=y.sign_exponent&0x8000;
    if(is_nan(x) || (op && is_nan(y))){
        uint16_t raised;
        if(!fnv_x87_calculate(FNV_X87_ADD,s->control,x,op?y:x,&z,&raised))return FNV_X87_STACK_UNSUPPORTED;
        flags|=raised;
    }else if(op==FNV_X87_CANDIDATE_F2XM1){
        unsigned e=exponent(x);
        if(e>0x3fff || (e==0x3fff && !one(x)))return FNV_X87_STACK_UNSUPPORTED;
        if(zero(x))z=x;
        else if(one(x)){
            if(x.sign_exponent&0x8000)z=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0xbffe};
            else if(rc==1 || rc==3)z=(struct fnv_x87_extended){UINT64_MAX,0x3ffe};
            else {z=x;flags|=0x200;}
            flags|=0x20;
        }else{
            if(!bounded(x))return FNV_X87_STACK_UNSUPPORTED;
            union quad q=evaluate(op,x,y);
            if(!narrow(q,rc,&z,&flags))return FNV_X87_STACK_UNSUPPORTED;
            flags|=0x20;
        }
    }else if(op==FNV_X87_CANDIDATE_FPATAN){
        if(!bounded(x) || !bounded(y))return FNV_X87_STACK_UNSUPPORTED;
        if((zero(y) && !(x.sign_exponent&0x8000)) || (is_infinity(x) && !(x.sign_exponent&0x8000) && !is_infinity(y)))z=special(ys,0);
        else{
            union quad q=evaluate(op,x,y);
            if(!narrow(q,rc,&z,&flags))return FNV_X87_STACK_UNSUPPORTED;
            flags|=0x20;
        }
    }else{
        int below=exponent(x)<0x3fff;
        if((x.sign_exponent&0x8000 && !zero(x)) || (zero(x) && zero(y)) || (one(x) && is_infinity(y)) || (is_infinity(x) && zero(y))){
            z=(struct fnv_x87_extended){UINT64_C(0xc000000000000000),0xffff};flags|=1;
        }else if(zero(x)){z=special(ys^0x8000,1);if(!is_infinity(y))flags|=4;}
        else if(is_infinity(x)){z=special(ys,1);}
        else if(one(x)){z=special(ys,0);}
        else if(zero(y)){z=special(ys^(below?0x8000:0),0);}
        else if(is_infinity(y)){z=special(ys^(below?0x8000:0),1);}
        else{
            if(!bounded(x) || !bounded(y))return FNV_X87_STACK_UNSUPPORTED;
            union quad q=evaluate(op,x,y);
            if(!narrow(q,rc,&z,&flags))return FNV_X87_STACK_UNSUPPORTED;
            flags|=0x20;
        }
    }
    s->registers[d]=z;s->status=(uint16_t)((s->status&~0x200u)|flags);
    if(op){s->occupied&=(uint8_t)~(1u<<t);s->top=(uint8_t)d;}
    s->status=(uint16_t)((s->status&~0x3800u)|((unsigned)s->top<<11));return FNV_X87_STACK_OK;
}
#else
enum fnv_x87_stack_result fnv_x87_transcendental_candidate(struct fnv_x87_stack *s,unsigned op){
    if(!s || s->top>7 || op>FNV_X87_CANDIDATE_FYL2X)return FNV_X87_STACK_INVALID;
    return FNV_X87_STACK_UNSUPPORTED;
}
#endif
