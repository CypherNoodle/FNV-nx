/* Synthetic exact-key replay and cache-miss fault test. No game functions. */
#include "fnv_sdk_runtime.h"
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <windows.h>
#include "startup_math_state.h"
#undef NDEBUG
#include <assert.h>
int fnv_startup_math_replay_open(const char *);
int fnv_startup_math_replay_enable_empty_delta(void);
void fnv_startup_math_replay_close(void);
extern _Atomic unsigned fnv_probe_math_hits,fnv_probe_math_misses;
extern _Atomic unsigned fnv_probe_math_empty_delta_hits;
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
    assert(argc==3 || argc==4);
    if(!strcmp(argv[2],"delta") || !strcmp(argv[2],"delta-reject") || !strcmp(argv[2],"delta-unsafe"))assert(fnv_startup_math_replay_enable_empty_delta());
    int ok=fnv_startup_math_replay_open(argv[1]);
    if(!strcmp(argv[2],"reject") || !strcmp(argv[2],"delta-reject")){assert(!ok);puts("REJECTED");return 0;}
    assert(ok);struct fnv_x87_stack s;fnv_x87_stack_init(&s);
    for(unsigned i=0;i<8;i++)s.registers[i]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
    s.registers[0]=(struct fnv_x87_extended){0,0};s.top=0;s.occupied=3;
    unsigned result=fnv_x87_transcendental_candidate(&s,0);assert(!result && fnv_probe_math_hits==1 && !fnv_probe_math_misses);
    assert(!s.registers[0].significand && !s.registers[0].sign_exponent && s.top==0 && s.occupied==3);
    s.control=0x77f;struct fnv_x87_stack before=s;
    if(!setjmp(jump)){fnv_x87_transcendental_candidate(&s,0);assert(!"Unknown state must stop");}
    assert(!memcmp(&s,&before,sizeof s) && fnv_probe_math_hits==1 && fnv_probe_math_misses==1);
    if(argc==4){
        FILE *file=fopen(argv[3],"rb");assert(file);unsigned char header[16],row[178],packed[86];assert(fread(header,1,16,file)==16);
        unsigned count=header[8]|(unsigned)header[9]<<8|(unsigned)header[10]<<16|(unsigned)header[11]<<24;
        for(unsigned i=0;i<count;i++){
            assert(fread(row,1,178,file)==178);startup_math_unpack(&s,row+2);before=s;
            if(strcmp(argv[2],"delta")){
                if(!setjmp(jump)){fnv_x87_trig_candidate(&s,row[1]);assert(!"Disabled/unsafe delta must miss");}
                assert(!memcmp(&s,&before,sizeof s)&&!fnv_probe_math_empty_delta_hits);break;
            }
            assert(fnv_x87_trig_candidate(&s,row[1])==FNV_X87_STACK_OK);startup_math_pack(packed,&s);assert(!memcmp(packed,row+92,86));
        }assert(!fclose(file));
        if(!strcmp(argv[2],"delta")){
            assert(fnv_probe_math_empty_delta_hits==count);
            startup_math_unpack(&s,row+2);s.registers[1].significand^=1;before=s;
            if(!setjmp(jump)){fnv_x87_trig_candidate(&s,row[1]);assert(!"Changed occupied register must miss");}assert(!memcmp(&s,&before,sizeof s));
            startup_math_unpack(&s,row+2);s.registers[2].significand^=1;before=s;
            if(!setjmp(jump)){fnv_x87_trig_candidate(&s,3);assert(!"Pushing trig must not use delta fallback");}assert(!memcmp(&s,&before,sizeof s));
            for(unsigned field=80;field<86;field++){
                unsigned char changed[86];memcpy(changed,row+2,86);changed[field]^=1;startup_math_unpack(&s,changed);before=s;
                if(!setjmp(jump)){fnv_x87_trig_candidate(&s,row[1]);assert(!"Changed control/status/top/mask must miss");}assert(!memcmp(&s,&before,sizeof s));
            }
        }
    }
    if(!strcmp(argv[2],"threads")){
        unsigned old_kind=fnv_probe_math_kind;HANDLE threads[2];
        for(unsigned i=0;i<2;i++){threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)i,0,NULL);assert(threads[i]);}
        assert(WaitForMultipleObjects(2,threads,TRUE,10000)==WAIT_OBJECT_0);
        for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code && CloseHandle(threads[i]));}
        assert(fnv_probe_math_kind==old_kind && fnv_probe_math_hits==2001 && fnv_probe_math_misses==1);
    }
    fnv_startup_math_replay_close();puts("EXACT_HIT_AND_EXPLICIT_MISS");return 0;
}
