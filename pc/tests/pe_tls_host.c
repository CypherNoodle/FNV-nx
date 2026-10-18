#include "pe_tls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
static void put(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i));}
static uint32_t get(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);unsigned char *arena=calloc(1,128u*1024*1024);assert(arena);struct fnv_native_image image;
    assert(fnv_native_load_image(argv[1],arena,128u*1024*1024,&image)==FNV_NATIVE_OK);struct fnv_pe_tls *plan=NULL;
    assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_OK && plan);struct fnv_pe_tls_info info;fnv_pe_tls_describe(plan,&info);
    if(argc==3){printf("{\"status\":\"PASS\",\"initialized_bytes\":%u,\"zero_bytes\":%u,\"alignment\":%u,\"index_address\":%u,\"callbacks\":\"empty\",\"game_executed\":false}\n",info.initialized_bytes,info.zero_bytes,info.alignment,info.index_address);fnv_pe_tls_destroy(plan);free(arena);return 0;}
    fnv_pe_tls_destroy(plan);assert(info.initialized_bytes==32 && info.zero_bytes==24 && info.alignment==16);
    unsigned char *d=arena+0x1b80,*index=arena+0x1ba4,*initial=arena+0x1bb0;unsigned char saved[24],data[2048],expected[32];memcpy(saved,d,24);
    unsigned seed=0x74cdb,checks=0;
    for(unsigned trial=0;trial<2000;++trial){seed=seed*1664525u+1013904223u;unsigned n=seed%33,z=(seed>>16)%1025,a=trial%15;
        for(unsigned j=0;j<32;++j)expected[j]=(unsigned char)(seed+j*13);
        memcpy(initial,expected,32);put(d+4,0x401bb0+n);put(d+16,z);put(d+20,a<<20);put(index,0xffffffff);
        plan=NULL;assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_OK && plan && get(index)==0xffffffff);
        fnv_pe_tls_describe(plan,&info);assert(info.initialized_bytes==n && info.zero_bytes==z && info.alignment==(a?1u<<(a-1):16));
        memset(data,0xa5,sizeof(data));if(n+z){assert(!fnv_pe_tls_initialize(plan,data,n+z-1));for(unsigned j=0;j<sizeof(data);++j)assert(data[j]==0xa5);}
        memset(initial,0xbb,32);assert(fnv_pe_tls_initialize(plan,data,sizeof(data)));
        assert(!memcmp(data,expected,n));for(unsigned j=n;j<n+z;++j)assert(data[j]==0);assert(data[n+z]==0xa5);
        fnv_pe_tls_bind_index(plan);assert(!get(index));fnv_pe_tls_destroy(plan);checks++;
    }
    unsigned negatives=0;memcpy(d,saved,24);
    const struct {unsigned offset;uint32_t value;enum fnv_native_result result;} bad[]={
        {0,0x401bd1,FNV_NATIVE_FORMAT},{8,0,FNV_NATIVE_FORMAT},{8,0x401ba5,FNV_NATIVE_FORMAT},
        {8,0x400004,FNV_NATIVE_FORMAT},{8,0x401bb0,FNV_NATIVE_FORMAT},{8,0x401b80,FNV_NATIVE_FORMAT},
        {12,0x7ffff000,FNV_NATIVE_FORMAT},{12,0x401ba1,FNV_NATIVE_FORMAT},
        {16,FNV_PE_TLS_MAX_BYTES,FNV_NATIVE_CAPACITY},{20,0xf00000,FNV_NATIVE_FORMAT},{20,1,FNV_NATIVE_FORMAT}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){memcpy(d,saved,24);put(d+bad[i].offset,bad[i].value);plan=NULL;
        assert(fnv_pe_tls_prepare(&image,&plan)==bad[i].result && !plan);negatives++;}
    memcpy(d,saved,24);put(arena+0x1ba0,0x401c00);assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_UNRESOLVED && !plan);negatives++;
    put(arena+0x1ba0,0);put(arena+0x19c,0x60000020);assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_FORMAT && !plan);negatives++;
    put(arena+0x19c,0xe0000020);put(arena+0x98+172,23);assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_FORMAT && !plan);negatives++;
    put(arena+0x98+172,0xffffffff);assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_FORMAT && !plan);negatives++;
    put(arena+0x98+172,24);image.tls_rva++;assert(fnv_pe_tls_prepare(&image,&plan)==FNV_NATIVE_FORMAT && !plan);negatives++;
    free(arena);printf("{\"status\":\"PASS\",\"tls_template_cases\":%u,\"negative_cases\":%u,\"snapshot_checked\":true,\"model_is_windows_oracle\":false}\n",checks,negatives);return 0;
}
