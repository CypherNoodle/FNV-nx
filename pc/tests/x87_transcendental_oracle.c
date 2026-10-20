/* Synthetic native x87 oracle: no executable or game inputs. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
int main(void) {
    if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
    unsigned char input[24],out[24];
    for(;;) {
        size_t n=fread(input,1,sizeof(input),stdin);if(!n)break;if(n!=sizeof(input))return 2;
        uint16_t old,cw=(uint16_t)(input[20]|input[21]<<8),sw;
        __asm__ volatile("fnstcw %0":"=m"(old));
        __asm__ volatile("fninit;fldcw %0"::"m"(cw));
        memset(out,0,sizeof(out));
#define EXEC(op) __asm__ volatile("fldt 10(%%rsi);fldt (%%rsi);" op ";fnstsw %0;fstpt (%%rdi)":"=m"(sw):"S"(input),"D"(out):"memory","st","st(1)")
        switch(input[22]) {
        case 0: EXEC("f2xm1");break;
        case 1: EXEC("fpatan");break;
        case 2: EXEC("fyl2x");break;
        default:return 3;
        }
        if(!input[22])__asm__ volatile("fstpt %0":"=m"(*(unsigned char (*)[10])(out+10))::"memory","st");
        out[20]=(unsigned char)sw;out[21]=(unsigned char)(sw>>8);
        out[22]=input[22]?0x80:0xc0;out[23]=input[22]?7:6;
        __asm__ volatile("fnclex;fninit;fldcw %0"::"m"(old));
        if(fwrite(out,1,sizeof(out),stdout)!=sizeof(out))return 4;
    }
    return ferror(stdin)?5:0;
}
