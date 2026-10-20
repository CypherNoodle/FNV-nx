/* Isolated candidate: exact integer comparisons of rounded and exact magnitudes.
 * Keeps the existing arithmetic payload/exception result and adds only C1. */
#include "x87_c1_candidate.h"
typedef __uint128_t wide;
static unsigned width(wide n){
    uint64_t hi=(uint64_t)(n>>64),lo=(uint64_t)n;
    return hi?128u-(unsigned)__builtin_clzll(hi):lo?64u-(unsigned)__builtin_clzll(lo):0;
}
static int compare(wide a,int ae,wide b,int be){
    unsigned aw=width(a),bw=width(b);
    if(!aw || !bw)return aw?1:bw?-1:0;
    int ah=ae+(int)aw,bh=be+(int)bw;
    if(ah!=bh)return ah>bh?1:-1;
    unsigned max=aw>bw?aw:bw;
    a<<=max-aw;b<<=max-bw;return a>b?1:a<b?-1:0;
}
static int exponent(struct fnv_x87_extended v){
    unsigned e=v.sign_exponent&0x7fffu;return (int)(e?e:1)-0x3fff-63;
}
static unsigned increment(enum fnv_x87_operation op,struct fnv_x87_extended a,
    struct fnv_x87_extended b,struct fnv_x87_extended z){
    if((z.sign_exponent&0x7fffu)==0x7fff)return z.significand==UINT64_C(0x8000000000000000);
    int ae=exponent(a),be=exponent(b),ze=exponent(z);
    if(op==FNV_X87_MUL)return compare(z.significand,ze,(wide)a.significand*b.significand,ae+be)>0;
    if(op==FNV_X87_DIV)return compare((wide)z.significand*b.significand,ze+be,a.significand,ae)>0;
    if(op==FNV_X87_SQRT)return compare((wide)z.significand*z.significand,ze+ze,a.significand,ae)>0;
    unsigned opposite=!!((a.sign_exponent^b.sign_exponent)&0x8000u)^(op==FNV_X87_SUB);
    if(!a.significand)return compare(z.significand,ze,b.significand,be)>0;
    if(!b.significand)return compare(z.significand,ze,a.significand,ae)>0;
    if(ae<be){struct fnv_x87_extended t=a;a=b;b=t;int e=ae;ae=be;be=e;}
    unsigned shift=(unsigned)(ae-be);
    if(shift>64){
        /* The small operand lies below half an ext80 ULP of the large one.
         * A different representable rounded magnitude is at least one ULP
         * away; an equal magnitude is above an exact difference only. */
        int c=compare(z.significand,ze,a.significand,ae);
        return c?c>0:opposite;
    }
    wide left=(wide)a.significand<<shift,right=b.significand;
    wide exact=opposite?(left>=right?left-right:right-left):left+right;
    return compare(z.significand,ze,exact,be)>0;
}
int fnv_x87_c1_candidate(enum fnv_x87_operation op,uint16_t cw,
    struct fnv_x87_extended a,struct fnv_x87_extended b,
    struct fnv_x87_extended *out,uint16_t *raised){
    if(!out || !raised)return 0;
    struct fnv_x87_extended z;uint16_t f;
    if(!fnv_x87_calculate(op,cw,a,b,&z,&f))return 0;
    unsigned up=(f&32u)?increment(op,a,b,z):0;
    *out=z;*raised=(uint16_t)((f&~0x200u)|(up<<9));return 1;
}
