/* Trusted, hash-verified ARM64 helper on stdin/stdout. Never a game backend. */
#include "startup_math_pipe.h"
#include "startup_math_state.h"
#include "host_mutex.h"
#include <stdio.h>
#include <string.h>
static fnv_host_mutex guard;
static int enabled,failed;
#define CACHE_SLOTS 4096u
/* Diagnostic transport memo only. Full request bytes remain the identity;
 * collisions evict, never alias. Access is serialized with the pipe itself. */
static unsigned char cache[CACHE_SLOTS][178],cache_live[CACHE_SLOTS];
static const char hex[]="0123456789abcdef";
static int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
int fnv_startup_math_pipe_open(void){
    if(enabled || !fnv_host_mutex_init(&guard))return 0;
    memset(cache_live,0,sizeof cache_live);failed=0;enabled=1;return 1;
}
void fnv_startup_math_pipe_close(void){if(enabled){fnv_host_mutex_destroy(&guard);enabled=0;failed=0;}}
int fnv_startup_math_pipe_resolve(struct fnv_x87_stack *s,unsigned kind,unsigned op,enum fnv_x87_stack_result *result){
    if(!enabled || !s || !result || kind>1 || op>(kind?3u:2u))return 0;
    unsigned char key[88],packet[178];char request[177],reply[384];
    key[0]=(unsigned char)kind;key[1]=(unsigned char)op;startup_math_pack(key+2,s);
    for(unsigned i=0;i<88;i++){request[i*2]=hex[key[i]>>4];request[i*2+1]=hex[key[i]&15];}request[176]=0;
    uint32_t hash=2166136261u;for(unsigned i=0;i<88;i++)hash=(hash^key[i])*16777619u;
    unsigned slot=hash&(CACHE_SLOTS-1u);
    fnv_host_mutex_lock(&guard);int ok=!failed;
    int hit=ok && cache_live[slot] && !memcmp(cache[slot],key,88);
    if(hit)memcpy(packet,cache[slot],sizeof packet);
    if(ok && !hit)ok=fprintf(stdout,"FNV_X87_REQUEST %s\n",request)>0 && !fflush(stdout) && fgets(reply,sizeof reply,stdin)!=NULL;
    if(ok && !hit)ok=strlen(reply)==14+356+1 && !memcmp(reply,"FNV_X87_REPLY ",14) && reply[370]=='\n';
    for(unsigned i=0;ok && !hit && i<178;i++){
        int a=digit(reply[14+i*2]),b=digit(reply[15+i*2]);if(a<0||b<0)ok=0;else packet[i]=(unsigned char)(a*16+b);
    }
    unsigned code=0;
    if(ok){code=(unsigned)packet[88]|(unsigned)packet[89]<<8|(unsigned)packet[90]<<16|(unsigned)packet[91]<<24;
        ok=!memcmp(packet,key,88) && code<=4 && (!code || !memcmp(packet+92,key+2,86)) && packet[176]<=7;}
    if(!ok)failed=1;
    else if(!hit){memcpy(cache[slot],packet,sizeof packet);cache_live[slot]=1;}
    /* Never trap while holding the transport mutex. No state change on error. */
    fnv_host_mutex_unlock(&guard);
    if(ok){startup_math_unpack(s,packet+92);*result=(enum fnv_x87_stack_result)code;}
    return ok;
}
