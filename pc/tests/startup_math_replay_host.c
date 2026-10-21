/* Synthetic exact-key replay and cache-miss fault test. No game functions. */
#include "fnv_sdk_runtime.h"
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#undef NDEBUG
#include <assert.h>
int fnv_startup_math_replay_open(const char *);
void fnv_startup_math_replay_close(void);
extern _Atomic unsigned fnv_probe_math_hits,fnv_probe_math_misses;
extern RECOMP_TLS unsigned fnv_probe_math_kind,fnv_probe_math_operation;
static jmp_buf jump;
void recomp_unimpl(const char *text,uint32_t at){(void)at;assert(strstr(text,"Target ARM64 x87"));longjmp(jump,1);}
static DWORD WINAPI worker(void *context){
    unsigned kind=(unsigned)(uintptr_t)context;
    for(unsigned n=0;n<1000;n++){
        struct fnv_x87_stack s;fnv_x87_stack_init(&s);
        for(unsigned i=0;i<8;i++)s.registers[i]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
        s.registers[0]=(struct fnv_x87_extended){0,0};s.occupied=3;
        unsigned result=kind?fnv_x87_trig_candidate(&s,0):fnv_x87_transcendental_candidate(&s,0);
        if(result || fnv_probe_math_kind!=kind || fnv_probe_math_operation || s.registers[0].significand)return 1;
    }
    return 0;
}
int main(int argc,char **argv){
    assert(argc==3);int ok=fnv_startup_math_replay_open(argv[1]);
    if(!strcmp(argv[2],"reject")){assert(!ok);puts("REJECTED");return 0;}
    assert(ok);struct fnv_x87_stack s;fnv_x87_stack_init(&s);
    for(unsigned i=0;i<8;i++)s.registers[i]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
    s.registers[0]=(struct fnv_x87_extended){0,0};s.top=0;s.occupied=3;
    unsigned result=fnv_x87_transcendental_candidate(&s,0);assert(!result && fnv_probe_math_hits==1 && !fnv_probe_math_misses);
    assert(!s.registers[0].significand && !s.registers[0].sign_exponent && s.top==0 && s.occupied==3);
    s.control=0x77f;struct fnv_x87_stack before=s;
    if(!setjmp(jump)){fnv_x87_transcendental_candidate(&s,0);assert(!"Unknown state must stop");}
    assert(!memcmp(&s,&before,sizeof s) && fnv_probe_math_hits==1 && fnv_probe_math_misses==1);
    if(!strcmp(argv[2],"threads")){
        unsigned old_kind=fnv_probe_math_kind;HANDLE threads[2];
        for(unsigned i=0;i<2;i++){threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)i,0,NULL);assert(threads[i]);}
        assert(WaitForMultipleObjects(2,threads,TRUE,10000)==WAIT_OBJECT_0);
        for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code && CloseHandle(threads[i]));}
        assert(fnv_probe_math_kind==old_kind && fnv_probe_math_hits==2001 && fnv_probe_math_misses==1);
    }
    fnv_startup_math_replay_close();puts("EXACT_HIT_AND_EXPLICIT_MISS");return 0;
}
