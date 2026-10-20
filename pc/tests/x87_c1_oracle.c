/* Synthetic native oracle captures C1 before FSTP clears it. No game code. */
#include "x87_c1_candidate.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static unsigned evaluations;
int fnv_counted_original(enum fnv_x87_operation op,uint16_t cw,
    struct fnv_x87_extended a,struct fnv_x87_extended b,
    struct fnv_x87_extended *out,uint16_t *flags){
    evaluations++;return fnv_x87_calculate(op,cw,a,b,out,flags);
}
static void native(const unsigned char *a,const unsigned char *b,uint16_t cw,
    unsigned op,unsigned char result[10],uint16_t *status){
    unsigned char saved[108];__asm__ volatile("fnsave %0":"=m"(saved)::"memory");
    __asm__ volatile("fninit; fldcw %0"::"m"(cw):"memory");
#define BINARY(bytes) __asm__ volatile("fldt %2; fldt %3; " bytes "; fnstsw %1; fstpt %0" \
    :"=m"(*(unsigned char (*)[10])result),"=m"(*status) \
    :"m"(*(const unsigned char (*)[10])a),"m"(*(const unsigned char (*)[10])b):"st","st(1)","memory")
    switch(op){
    case FNV_X87_ADD:BINARY(".byte 0xde,0xc1");break;
    case FNV_X87_SUB:BINARY(".byte 0xde,0xe9");break;
    case FNV_X87_MUL:BINARY(".byte 0xde,0xc9");break;
    case FNV_X87_DIV:BINARY(".byte 0xde,0xf9");break;
    default:__asm__ volatile("fldt %2; fsqrt; fnstsw %1; fstpt %0"
        :"=m"(*(unsigned char (*)[10])result),"=m"(*status)
        :"m"(*(const unsigned char (*)[10])a):"st","memory");break;
    }
#undef BINARY
    __asm__ volatile("frstor %0"::"m"(saved):"memory");
}
int main(void){
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    unsigned char in[32],out[40];
    while(fread(in,1,sizeof in,stdin)==sizeof in){
        unsigned op=in[0];uint16_t cw=(uint16_t)(in[2]|in[3]<<8),f,base,hw;
        struct fnv_x87_extended a=fnv_x87_unpack(in+4),b=fnv_x87_unpack(in+14),z,t;
        evaluations=0;
        if(!fnv_x87_c1_candidate((enum fnv_x87_operation)op,cw,a,b,&z,&f))return 2;
        fnv_x87_pack(out,z);memcpy(out+10,&f,2);
        native(in+4,in+14,cw,op,out+12,&hw);memcpy(out+22,&hw,2);
        if(!fnv_x87_calculate((enum fnv_x87_operation)op,cw,a,b,&t,&base))return 3;
        fnv_x87_pack(out+24,t);memcpy(out+34,&base,2);memcpy(out+36,&evaluations,4);
        if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 4;
    }
    return ferror(stdin)?1:0;
}
