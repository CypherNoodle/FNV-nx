#ifndef FNV_X87_QUAD_BITS_H
#define FNV_X87_QUAD_BITS_H
#include "x87_extended.h"
#include <float.h>
_Static_assert(sizeof(long double)==16 && LDBL_MANT_DIG==113 && LDBL_MAX_EXP==16384,"binary128 required");
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Require little endian
#endif
union quad {long double value;uint64_t words[2];};
static inline union quad load(struct fnv_x87_extended x){
    union quad q;q.words[0]=x.significand<<49;
    q.words[1]=((uint64_t)x.sign_exponent<<48)|((x.significand>>15)&UINT64_C(0xffffffffffff));return q;
}
/* Round the kernel's normal binary128 approximation to extended precision.
 * C1 describes that narrowing, not physical x87 microcode rounding. */
static inline int narrow(union quad q,unsigned rc,struct fnv_x87_extended *out,uint16_t *flags){
    uint64_t lo=q.words[0],hi=q.words[1],lost=lo&UINT64_C(0x1ffffffffffff);
    unsigned e=(unsigned)(hi>>48)&0x7fff,sign=(unsigned)(hi>>48)&0x8000;
    if(!e || e==0x7fff)return 0;
    uint64_t sig=UINT64_C(0x8000000000000000)|((hi&UINT64_C(0xffffffffffff))<<15)|(lo>>49);
    int up=rc==0 ? lost>UINT64_C(0x1000000000000) || (lost==UINT64_C(0x1000000000000) && (sig&1)) :
        rc==1 ? sign && lost : rc==2 ? !sign && lost : 0;
    if(up){if(++sig==0){sig=UINT64_C(0x8000000000000000);e++;}*flags|=0x200;}
    if(e>=0x7fff)return 0;
    *out=(struct fnv_x87_extended){sig,(uint16_t)(sign|e)};return 1;
}
#endif
