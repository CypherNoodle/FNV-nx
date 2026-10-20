/* Original candidate: reconstruct the magnitude increment used by x87 C1. */
#include "x87_c1_candidate.h"
int fnv_x87_c1_candidate(enum fnv_x87_operation op,uint16_t cw,
    struct fnv_x87_extended a,struct fnv_x87_extended b,
    struct fnv_x87_extended *out,uint16_t *raised){
    if(!out || !raised)return 0;
    struct fnv_x87_extended value;uint16_t flags;
    if(!fnv_x87_calculate(op,cw,a,b,&value,&flags))return 0;
    unsigned up=0,rc=(cw>>10)&3u;
    if(flags&32u){
        if(rc==1)up=!!(value.sign_exponent&0x8000u);
        else if(rc==2)up=!(value.sign_exponent&0x8000u);
        else if(rc==0){
            struct fnv_x87_extended truncated;uint16_t discarded;
            if(!fnv_x87_calculate(op,(uint16_t)((cw&~0xc00u)|0xc00u),a,b,&truncated,&discarded))return 0;
            unsigned e=value.sign_exponent&0x7fff,t=truncated.sign_exponent&0x7fff;
            up=e>t || (e==t && value.significand>truncated.significand);
        }
    }
    *out=value;*raised=(uint16_t)((flags&~0x200u)|(up<<9));return 1;
}
