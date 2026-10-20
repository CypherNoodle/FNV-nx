/* Readback-only 32-bit x87 FLDCW oracle. No FP arithmetic or game execution. */
#include <stdint.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#if !defined(__i386__)
#error Build for the 32-bit x86 guest instruction set
#endif
int main(void){
    _setmode(_fileno(stdout),_O_BINARY);unsigned char saved[108];
    __asm__ volatile("fnsave %0":"=m"(saved)::"memory");
    for(unsigned n=0;n<65536;n++){
        uint16_t in=(uint16_t)n,out;
        __asm__ volatile("fninit; fldcw %1; fnstcw %0":"=m"(out):"m"(in):"memory");
        if(fwrite(&out,1,2,stdout)!=2)return 2;
    }
    __asm__ volatile("frstor %0"::"m"(saved):"memory");return 0;
}
