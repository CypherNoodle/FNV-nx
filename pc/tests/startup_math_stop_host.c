/* Diagnostic-only host functions. Target ARM64 math is never guessed here. */
#include "fnv_sdk_runtime.h"
#include "cpu_runtime.h"
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
#include "startup_math_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
RECOMP_TLS unsigned fnv_probe_math_kind=UINT32_MAX,fnv_probe_math_operation=UINT32_MAX;
_Atomic unsigned fnv_probe_math_hits,fnv_probe_math_misses;
static unsigned char *records;
static uint32_t record_count;
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int fnv_startup_math_replay_open(const char *path){
    if(!path || !*path)return 1;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    unsigned char header[16];int ok=fread(header,1,16,f)==16 && !memcmp(header,"FNVX87R1",8) && !read32(header+12);
    uint32_t count=ok?read32(header+8):0;
    if(!ok || count>4096){fclose(f);return 0;}
    size_t bytes=(size_t)count*178;unsigned char *data=malloc(bytes?bytes:1);
    if(!data){fclose(f);return 0;}
    ok=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f);
    if(fclose(f))ok=0;
    for(uint32_t i=0;ok && i<count;i++){
        const unsigned char *p=data+(size_t)i*178;uint32_t result=read32(p+88);
        if(p[0]>1 || p[1]>(p[0]?3:2) || result>4 || (result && memcmp(p+2,p+92,86)))ok=0;
        for(uint32_t j=0;ok && j<i;j++)if(!memcmp(p,data+(size_t)j*178,88))ok=0;
    }
    if(!ok){free(data);return 0;}records=data;record_count=count;return 1;
}
void fnv_startup_math_replay_close(void){free(records);records=NULL;record_count=0;}
static int replay(struct fnv_x87_stack *s,unsigned kind,unsigned op,enum fnv_x87_stack_result *result){
    unsigned char key[88];key[0]=(unsigned char)kind;key[1]=(unsigned char)op;startup_math_pack(key+2,s);
    for(uint32_t i=0;i<record_count;i++){
        const unsigned char *p=records+(size_t)i*178;
        if(!memcmp(key,p,88)){startup_math_unpack(s,p+92);*result=(enum fnv_x87_stack_result)read32(p+88);fnv_probe_math_hits++;return 1;}
    }
    fnv_probe_math_misses++;return 0;
}
enum fnv_x87_stack_result fnv_x87_transcendental_candidate(struct fnv_x87_stack *s,unsigned op){
    fnv_probe_math_kind=0;fnv_probe_math_operation=op;enum fnv_x87_stack_result result;
    if(op<=2 && replay(s,0,op,&result))return result;
    recomp_unimpl("Target ARM64 x87 math reached in host CRT diagnostic",0);
    return FNV_X87_STACK_UNSUPPORTED;
}
enum fnv_x87_stack_result fnv_x87_trig_candidate(struct fnv_x87_stack *s,unsigned op){
    fnv_probe_math_kind=1;fnv_probe_math_operation=op;enum fnv_x87_stack_result result;
    if(op<=3 && replay(s,1,op,&result))return result;
    recomp_unimpl("Target ARM64 x87 trig reached in host CRT diagnostic",0);
    return FNV_X87_STACK_UNSUPPORTED;
}
