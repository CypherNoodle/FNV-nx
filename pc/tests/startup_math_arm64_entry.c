/* Diagnostic evaluator for exact compiled target math, never host libm. */
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
#include "startup_math_state.h"
static int error_value;
int *__errno(void){return &error_value;}
void *memset(void *p,int c,__SIZE_TYPE__ n){volatile unsigned char *b=p;for(__SIZE_TYPE__ i=0;i<n;i++)b[i]=(unsigned char)c;return p;}
__attribute__((section(".text.entry"))) unsigned probe(const unsigned char *input,unsigned char *output){
    struct fnv_x87_stack state;startup_math_unpack(&state,input+2);
    unsigned result=input[0]==0?fnv_x87_transcendental_candidate(&state,input[1]):
        input[0]==1?fnv_x87_trig_candidate(&state,input[1]):FNV_X87_STACK_INVALID;
    startup_math_pack(output,&state);return result;
}
