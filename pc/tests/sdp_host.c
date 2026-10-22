#include "d3d9_sdp.h"
#include "d3d9_com.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
int main(int argc,char **argv) {
    assert(argc==3);
    struct fnv_d3d9_sdp *p=(void *)1;
    enum fnv_sdp_result r=fnv_sdp_load(argv[1],(size_t)strtoull(argv[2],NULL,10),&p);
    printf("%u\n",r);
    if(r){assert(!p);return 0;}
    size_t count;const struct fnv_d3d9_shader_code *e=fnv_sdp_entries(p,&count);
    printf("%zu\n",count);
    for(size_t i=0;i<count;i++) {
        uint32_t hash=2166136261u;
        const unsigned char *b=e[i].bytes;
        for(size_t j=0;j<e[i].size;j++)hash=(hash^b[j])*16777619u;
        printf("%u %08x\n",e[i].size,hash);
    }
    /* Memory parsing owns an independent copy, including after source poison. */
    FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=12);rewind(f);
    unsigned char *data=malloc((size_t)n);assert(data && fread(data,1,(size_t)n,f)==(size_t)n);assert(!fclose(f));
    struct fnv_d3d9_sdp *copy=NULL;assert(fnv_sdp_parse(data,(size_t)n,(size_t)strtoull(argv[2],NULL,10),&copy)==FNV_SDP_OK);
    memset(data,0,(size_t)n);free(data);
    size_t copied;const struct fnv_d3d9_shader_code *ce=fnv_sdp_entries(copy,&copied);assert(copied==count);
    for(size_t i=0;i<count;i++)assert(ce[i].size==e[i].size && !memcmp(ce[i].bytes,e[i].bytes,e[i].size));
    fnv_sdp_destroy(copy);fnv_sdp_destroy(p);fnv_sdp_destroy(NULL);return 0;
}
