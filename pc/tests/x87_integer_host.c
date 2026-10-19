#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void oracle(const unsigned char *a,const unsigned char *integer,uint16_t cw,unsigned mode,unsigned char out[12]) {
    uint16_t old,status;memset(out,0,12);
    __asm__ volatile("fnstcw %0" : "=m"(old));__asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
#define LOAD(bytes,text) __asm__ volatile(text "; fstpt %0" : "=m"(*(unsigned char (*)[10])out) : "m"(*(const unsigned char (*)[bytes])integer) : "st")
#define STORE(bytes,text) __asm__ volatile("fldt %1; " text : "=m"(*(unsigned char (*)[bytes])out) : "m"(*(const unsigned char (*)[10])a) : "st")
    switch(mode) {
    case 0:LOAD(2,"filds %1");break;
    case 1:LOAD(4,"fildl %1");break;
    case 2:LOAD(8,"fildll %1");break;
    case 3:STORE(2,"fistps %0");break;
    case 4:STORE(4,"fistpl %0");break;
    case 5:STORE(8,"fistpll %0");break;
    case 6:STORE(2,"fisttps %0");break;
    case 7:STORE(4,"fisttpl %0");break;
    case 8:STORE(8,"fisttpll %0");break;
    case 9:STORE(10,"frndint; fstpt %0");break;
    case 10:STORE(2,"fists %0; .byte 0xdd,0xd8");break;
    default:STORE(4,"fistl %0; .byte 0xdd,0xd8");break;
    }
#undef LOAD
#undef STORE
    __asm__ volatile("fnstsw %0" : "=m"(status));status&=0x3f;
    out[10]=(unsigned char)status;out[11]=(unsigned char)(status>>8);
    __asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
    if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
    unsigned char request[24];unsigned count=0;
    for(;;) {
        size_t n=fread(request,1,24,stdin);if(!n)break;if(n!=24 || request[22]>11)return 2;
        uint16_t cw=(uint16_t)(request[20]|request[21]<<8);unsigned mode=request[22];
        unsigned char expected[12],actual[16]={0};oracle(request,request+10,cw,mode,expected);
        struct fnv_x87_stack state;fnv_x87_stack_init(&state);state.control=cw;
        unsigned width=mode%3==0 ? 2 : mode%3==1 ? 4 : 8;
        if(mode<3) {
            if(fnv_x87_stack_load_integer(&state,request+10,width)!=FNV_X87_STACK_OK || fnv_x87_stack_store_pop(&state,actual)!=FNV_X87_STACK_OK)return 3;
        } else {
            if(fnv_x87_stack_load(&state,request)!=FNV_X87_STACK_OK)return 4;
            if(mode==9) {
                if(fnv_x87_stack_round_integer(&state)!=FNV_X87_STACK_OK || fnv_x87_stack_store_pop(&state,actual)!=FNV_X87_STACK_OK)return 5;
            } else {
                if(mode>=10)width=mode==10 ? 2 : 4;
                if(fnv_x87_stack_store_integer(&state,actual,width,mode<10,mode>=6 && mode<=8)!=FNV_X87_STACK_OK)return 6;
                if(mode>=10 && fnv_x87_stack_store_register(&state,0,1)!=FNV_X87_STACK_OK)return 7;
            }
        }
        actual[10]=(unsigned char)(state.status&0x3f);
        if(memcmp(actual,expected,12) || state.top || state.occupied) {
            fprintf(stderr,"Mismatch %u mode %u cw %04X status %04X/%04X\n",count,mode,cw,state.status,expected[10]);return 8;
        }
        if(fwrite(expected,1,12,stdout)!=12)return 9;count++;
    }
    fprintf(stderr,"PASS %u integer x87 cases\n",count);return ferror(stdin)?10:0;
}
