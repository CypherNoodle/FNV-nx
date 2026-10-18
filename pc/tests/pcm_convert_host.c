#include "pcm_convert.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#undef NDEBUG
#include <assert.h>
struct input {uint32_t n,at,seed;};
static int source(void *p,int16_t out[2]){struct input *s=p;if(s->at==s->n)return 0;
    for(unsigned i=0;i<2;i++){s->seed=s->seed*1664525u+1013904223u;out[i]=(int16_t)(s->seed>>16);}s->at++;return 1;}
static int error(void *p,int16_t out[2]){(void)p;(void)out;return -1;}
int main(int argc,char **argv){
    assert(argc==6);uint32_t rate=(uint32_t)strtoul(argv[1],0,10),frames=(uint32_t)strtoul(argv[2],0,10),chunk=(uint32_t)strtoul(argv[3],0,10);
    struct fnv_pcm_converter c;assert(fnv_pcm_convert_init(&c,rate)==FNV_CONVERT_OK && chunk && chunk<=4096);
    struct input s={frames,0,12345};FILE *f=fopen(argv[4],"wb");assert(f);int16_t out[8192];uint64_t total=0;
    for(;;){uint32_t n=999;enum fnv_convert_result r=fnv_pcm_convert_read(&c,source,&s,out,chunk,&n);assert(r==FNV_CONVERT_OK || r==FNV_CONVERT_EOF);
        if(r==FNV_CONVERT_EOF){assert(!n);break;}assert(n && fwrite(out,4,n,f)==n);total+=n;}
    assert(fclose(f)==0 && total==((uint64_t)frames*48000+rate-1)/rate);
    assert(fnv_pcm_convert_init(&c,rate)==FNV_CONVERT_OK);uint32_t n=99;
    assert(fnv_pcm_convert_read(&c,error,0,out,1,&n)==FNV_CONVERT_SOURCE_ERROR && !n);
    assert(fnv_pcm_convert_init(&c,0)==FNV_CONVERT_INVALID && fnv_pcm_convert_init(&c,96000)==FNV_CONVERT_INVALID);
    (void)argv[5];puts("PASS");return 0;
}
