#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

static void oracle(const unsigned char *a,const unsigned char *b,uint16_t cw,unsigned mode,unsigned char out[8]) {
    uint16_t old,status,control;
    __asm__ volatile("fnstcw %0" : "=m"(old));
    __asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
#define SEQ(text) __asm__ volatile("fldt %0; fldt %1; " text : : \
    "m"(*(const unsigned char (*)[10])a),"m"(*(const unsigned char (*)[10])b) : "st","st(1)")
    switch(mode) {
    case 0:SEQ(".byte 0xde,0xd9");break;
    case 1:SEQ(".byte 0xda,0xe9");break;
    case 2:SEQ(".byte 0xd8,0xd9,0xdd,0xd8");break;
    case 3:SEQ(".byte 0xdd,0xe9,0xdd,0xd8");break;
    case 4:SEQ(".byte 0xde,0xd9; fnclex");break;
    default:SEQ(".byte 0xde,0xd9; fninit");break;
    }
#undef SEQ
    __asm__ volatile("fnstsw %0; fnstcw %1" : "=m"(status),"=m"(control));
    uint32_t ax=0xa5a5feedu;__asm__ volatile("fnstsw %%ax" : "+a"(ax));
    out[0]=(unsigned char)status;out[1]=(unsigned char)(status>>8);
    for(unsigned i=0;i<4;i++)out[2+i]=(unsigned char)(ax>>(i*8));
    out[6]=(unsigned char)control;out[7]=(unsigned char)(control>>8);
    __asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
    if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
    unsigned char input[24];unsigned count=0;
    for(;;) {
        size_t n=fread(input,1,24,stdin);if(!n)break;if(n!=24 || input[22]>5)return 2;
        uint16_t cw=(uint16_t)(input[20]|(uint16_t)input[21]<<8);
        unsigned mode=input[22];unsigned char out[8];oracle(input,input+10,cw,mode,out);
        struct fnv_x87_stack state;fnv_x87_stack_init(&state);
        if(fnv_x87_stack_control(&state,input+20)!=FNV_X87_STACK_OK ||
           fnv_x87_stack_load(&state,input)!=FNV_X87_STACK_OK ||
           fnv_x87_stack_load(&state,input+10)!=FNV_X87_STACK_OK ||
           fnv_x87_stack_compare(&state,1,mode==1 || mode==3,mode==2 || mode==3 ? 1 : 2)!=FNV_X87_STACK_OK)return 3;
        if(mode==2 || mode==3)if(fnv_x87_stack_store_register(&state,0,1)!=FNV_X87_STACK_OK)return 4;
        if(mode==4)fnv_x87_stack_clear_exceptions(&state);
        if(mode==5)fnv_x87_stack_init(&state);
        if(state.top || state.occupied || state.status!=(uint16_t)(out[0]|out[1]<<8) ||
           state.control!=(uint16_t)(out[6]|out[7]<<8)) {
            fprintf(stderr,"Mismatch %u mode %u cw %04X status %04X/%04X\n",count,mode,cw,state.status,out[0]|out[1]<<8);return 5;
        }
        if(fwrite(out,1,8,stdout)!=8)return 6;count++;
    }
    fprintf(stderr,"PASS %u x87 comparison/control cases\n",count);return ferror(stdin)?7:0;
}
