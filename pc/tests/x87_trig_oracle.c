/* Native x87 trig oracle. Synthetic requests only; captures stack results. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
int main(void){
    if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
    unsigned char in[16],out[40];
    for(;;){
        size_t n=fread(in,1,16,stdin);if(!n)break;if(n!=16)return 2;
        uint16_t old,cw=(uint16_t)(in[10]|in[11]<<8),sw;
        __asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fninit;fldcw %0"::"m"(cw));
        memset(out,0,sizeof(out));
        if(in[13])__asm__ volatile("fld1;fld1;fld1;fld1;fld1;fld1;fld1":::"st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)");
        else __asm__ volatile("fld1":::"st");
        __asm__ volatile("fldt %0"::"m"(*(unsigned char (*)[10])in):"st");
        switch(in[12]){
        case 0:__asm__ volatile("fsin":::"st");break;
        case 1:__asm__ volatile("fcos":::"st");break;
        case 2:__asm__ volatile("fptan":::"st","st(1)");break;
        case 3:__asm__ volatile("fsincos":::"st","st(1)");break;
        default:return 3;
        }
        __asm__ volatile("fnstsw %0":"=m"(sw));
        unsigned top=(sw>>11)&7,pushed=(!in[13] && top==5);
        unsigned count=in[13]?3:2+pushed;
        for(unsigned i=0;i<count;i++)__asm__ volatile("fstpt %0":"=m"(*(unsigned char (*)[10])(out+10*i))::"memory","st");
        out[30]=(unsigned char)sw;out[31]=(unsigned char)(sw>>8);out[32]=in[13]?0xff:pushed?0xe0:0xc0;out[33]=(unsigned char)top;
        __asm__ volatile("fnclex;fninit;fldcw %0"::"m"(old));
        if(fwrite(out,1,40,stdout)!=40)return 4;
    }
    return ferror(stdin)?5:0;
}
