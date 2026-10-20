/* Synthetic 32-bit Windows oracle; never loads game code or changes ES. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#if !defined(__i386__)
#error Build this selector oracle for 32-bit x86
#endif
int main(void){
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    unsigned char request[12],saved[108],result[10],env[28],out[16];
    uint16_t ds,es;
    __asm__ volatile("mov %%ds,%0; mov %%es,%1":"=r"(ds),"=r"(es));
    if(ds!=es){fprintf(stderr,"ES does not alias DS: %x %x\n",es,ds);return 3;}
    while(fread(request,1,sizeof request,stdin)==sizeof request){
        float a,b;uint16_t cw;memcpy(&a,request,4);memcpy(&b,request+4,4);memcpy(&cw,request+8,2);
        __asm__ volatile("fnsave %0":"=m"(saved)::"memory");
        __asm__ volatile("fninit; fldcw %2; flds %3; fadds %%es:(%4); fnstenv %1; fstpt %0"
            :"=m"(result),"=m"(env):"m"(cw),"m"(a),"r"(&b):"memory");
        __asm__ volatile("frstor %0"::"m"(saved):"memory");
        memcpy(out,result,10);memcpy(out+10,env+4,2);memcpy(out+12,&ds,2);memcpy(out+14,&es,2);
        if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 2;
    }
    return ferror(stdin)?1:0;
}
