/* Native x86-64 oracle for the synthetic quad-math research harness. */
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
int main(void){if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;unsigned char input[24],out[12];for(;;){size_t n=fread(input,1,24,stdin);if(!n)break;if(n!=24)return 2;uint16_t old,cw=0x37f,sw;__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fninit;fldcw %0"::"m"(cw));
#define UNARY(op) __asm__ volatile("fldt %1;" op ";fnstsw %0;fstpt %2":"=m"(sw):"m"(*(unsigned char (*)[10])input),"m"(*(unsigned char (*)[10])out):"memory","st")
switch(input[20]){case 0:UNARY("fsin");break;case 1:UNARY("fcos");break;case 2:UNARY("fptan;fstp %%st(0)");break;case 3:__asm__ volatile("fldt %1;fldt %2;fpatan;fnstsw %0;fstpt %3":"=m"(sw):"m"(*(unsigned char (*)[10])input),"m"(*(unsigned char (*)[10])(input+10)),"m"(*(unsigned char (*)[10])out):"memory","st","st(1)");break;case 4:__asm__ volatile("fldt %2;fldt %1;fyl2x;fnstsw %0;fstpt %3":"=m"(sw):"m"(*(unsigned char (*)[10])input),"m"(*(unsigned char (*)[10])(input+10)),"m"(*(unsigned char (*)[10])out):"memory","st","st(1)");break;default:UNARY("f2xm1");break;}
out[10]=(unsigned char)sw;out[11]=(unsigned char)(sw>>8);__asm__ volatile("fnclex;fldcw %0"::"m"(old));if(fwrite(out,1,12,stdout)!=12)return 3;}return ferror(stdin)?4:0;}
