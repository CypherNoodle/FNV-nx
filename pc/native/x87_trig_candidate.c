/* Experimental trig backend with own integer reduction before target libm. */
#include "x87_trig_candidate.h"
#include <float.h>
#if defined(FNV_CPU_X87_QUAD_TRIG) && (!defined(__aarch64__) || LDBL_MANT_DIG!=113 || !defined(FNV_CPU_X87_QUAD_MATH))
#error Selected trig model requires the ARM64 quad backend
#endif
#if defined(__aarch64__) && LDBL_MANT_DIG==113
#include <math.h>
#include "x87_quad_bits.h"
#include "x87_two_over_pi.h"
static unsigned bit(const uint64_t p[5],int n){return n<0 || n>=320?0:(unsigned)((p[n/64]>>(n%64))&1);}
/* |x| >= 1/2, |x| < 2^63. Product is exact for the stored 256-bit constant.
 * Return the nearest quadrant and a signed reduced angle in [-pi/4,pi/4]. */
static int reduce_angle(struct fnv_x87_extended x,unsigned *quadrant,union quad *reduced){
    uint64_t p[5],carry=0;
    for(unsigned i=0;i<4;i++){
        __uint128_t z=(__uint128_t)x.significand*x87_two_over_pi[i]+carry;
        p[i]=(uint64_t)z;carry=(uint64_t)(z>>64);
    }
    p[4]=carry;int e=(x.sign_exponent&0x7fff)-0x3fff,s=319-e;
    unsigned q=bit(p,s)|(bit(p,s+1)<<1),half=bit(p,s-1),lower=0;
    for(int i=0;i<s-1;i++)lower|=bit(p,i);
    unsigned up=half && (lower || (q&1));*quadrant=(q+up)&3;
    unsigned top_bits=(unsigned)s-256;uint64_t mask=top_bits==64?UINT64_MAX:(UINT64_C(1)<<top_bits)-1;
    p[4]&=mask;
    if(up){
        uint64_t c=1;
        for(unsigned i=0;i<5;i++){uint64_t a=~p[i];p[i]=a+c;c=c && p[i]==0;}
        p[4]&=mask;
    }
    int h=319;while(h>=0 && !bit(p,h))h--;
    union quad f={.words={0,0}};
    /* The omitted constant tail changes this integer product by less than
     * 2^64. Require at least 96 relative guard bits in the reduced fraction;
     * uncertain near-zero remainders must not become guessed angles. */
    if(h<160)return 0;
    __uint128_t mant=0;
    for(int i=0;i<113;i++)mant=(mant<<1)|bit(p,h-i);
    unsigned guard=bit(p,h-113),sticky=0;
    for(int i=h-114;i>=0;i--)sticky|=bit(p,i);
    if(guard && (sticky || (mant&1)))mant++;
    int exponent=h-s+0x3fff;
    if(mant>>113){mant>>=1;exponent++;}
    f.words[0]=(uint64_t)mant;
    f.words[1]=((uint64_t)(exponent|(up?0x8000:0))<<48)|((uint64_t)(mant>>64)&UINT64_C(0xffffffffffff));
    f.value*=0x1.921fb54442d18469898cc51701b8p+0L;*reduced=f;return 1;
}
static int kernel(struct fnv_x87_extended x,unsigned op,union quad *sine,union quad *cosine){
    uint64_t fpcr,fpsr;
    __asm__ volatile("mrs %0,fpcr; mrs %1,fpsr; msr fpcr,xzr; msr fpsr,xzr":"=r"(fpcr),"=r"(fpsr)::"memory");
    unsigned quadrant=0,negative=x.sign_exponent&0x8000;union quad r;
    if((x.sign_exponent&0x7fff)<0x3ffe){struct fnv_x87_extended absolute=x;absolute.sign_exponent&=0x7fff;r=load(absolute);}
    else if(!reduce_angle(x,&quadrant,&r)){
        __asm__ volatile("msr fpcr,%0; msr fpsr,%1"::"r"(fpcr),"r"(fpsr):"memory");return 0;
    }
    if(op==FNV_X87_TRIG_FPTAN){
        sine->value=tanl(r.value);if(quadrant&1)sine->value=-1.0L/sine->value;
    }else{
        union quad a,b;a.value=sinl(r.value);b.value=cosl(r.value);
        switch(quadrant){
        case 0:*sine=a;*cosine=b;break;
        case 1:*sine=b;cosine->value=-a.value;break;
        case 2:sine->value=-a.value;cosine->value=-b.value;break;
        default:sine->value=-b.value;*cosine=a;break;
        }
    }
    if(negative)sine->words[1]^=UINT64_C(0x8000000000000000);
    __asm__ volatile("msr fpcr,%0; msr fpsr,%1"::"r"(fpcr),"r"(fpsr):"memory");
    return 1;
}
enum fnv_x87_stack_result fnv_x87_trig_candidate(struct fnv_x87_stack *s,unsigned op){
    if(!s || s->top>7 || op>FNV_X87_TRIG_FSINCOS)return FNV_X87_STACK_INVALID;
    if((s->control&0x3f)!=0x3f || ((s->control>>8)&3)==1 || (s->status&0x80))return FNV_X87_STACK_UNSUPPORTED;
    unsigned t=s->top,next=(t+7)&7,push=op>=FNV_X87_TRIG_FPTAN;
    if(!(s->occupied&(1u<<t)))return FNV_X87_STACK_EMPTY;
    struct fnv_x87_extended x=s->registers[t],a,b;unsigned e=x.sign_exponent&0x7fff;
    int nan=e==0x7fff && (x.significand&UINT64_C(0x7fffffffffffffff));
    if((!e && x.significand) || (e && !(x.significand>>63)) || (nan && !(x.significand&UINT64_C(0x4000000000000000))))return FNV_X87_STACK_UNSUPPORTED;
    if(push && (s->occupied&(1u<<next)))return FNV_X87_STACK_FULL;
    if(e!=0x7fff && e>=0x403e){s->status=(uint16_t)((s->status&~0x200u)|0x400u);return FNV_X87_STACK_OK;}
    uint16_t flags=0;unsigned rc=(s->control>>10)&3;
    b=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
    if(e==0x7fff){
        a=nan?x:(struct fnv_x87_extended){UINT64_C(0xc000000000000000),0xffff};
        if(!nan)flags|=1;
        if(push)b=a; /* Native FPTAN propagates NaN into both pushed values. */
    }else if(!e){a=x;if(op==FNV_X87_TRIG_FCOS)a=b;}
    else{
        if(e<0x3fff-1000)return FNV_X87_STACK_UNSUPPORTED;
        union quad sine,cosine;if(!kernel(x,op,&sine,&cosine))return FNV_X87_STACK_UNSUPPORTED;
        if(!narrow(op==FNV_X87_TRIG_FCOS?cosine:sine,rc,&a,&flags))return FNV_X87_STACK_UNSUPPORTED;
        if(op==FNV_X87_TRIG_FSINCOS){uint16_t ignored=0;if(!narrow(cosine,rc,&b,&ignored))return FNV_X87_STACK_UNSUPPORTED;flags=(uint16_t)((flags&~0x200u)|(ignored&0x200u));}
        flags|=0x20;
    }
    s->registers[t]=a;
    if(push){s->registers[next]=b;s->occupied|=(uint8_t)(1u<<next);s->top=(uint8_t)next;}
    s->status=(uint16_t)((s->status&~0x3e00u)|flags|((unsigned)s->top<<11));return FNV_X87_STACK_OK;
}
#else
enum fnv_x87_stack_result fnv_x87_trig_candidate(struct fnv_x87_stack *s,unsigned op){
    if(!s || s->top>7 || op>FNV_X87_TRIG_FSINCOS)return FNV_X87_STACK_INVALID;
    return FNV_X87_STACK_UNSUPPORTED;
}
#endif
