#include "pe_tls.h"
#include <stdlib.h>
#include <string.h>
struct fnv_pe_tls {struct fnv_pe_tls_info info;unsigned char *index;unsigned char initial[];};
static uint32_t get32(const unsigned char *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int intersects(uint32_t a,uint32_t n,uint32_t b,uint32_t m){return (uint64_t)a+n>b && (uint64_t)b+m>a;}
enum fnv_native_result fnv_pe_tls_prepare(const struct fnv_native_image *image,struct fnv_pe_tls **out){
    if(!out)return FNV_NATIVE_FORMAT;
    *out=NULL;if(!image || !image->data || image->headers>image->size || image->headers<64 ||
        !image->section_count || image->section_count>FNV_NATIVE_MAX_SECTIONS)return FNV_NATIVE_FORMAT;
    uint32_t nt=get32(image->data+60);
    if((uint64_t)nt+24+176>image->headers)return FNV_NATIVE_FORMAT;
    const unsigned char *optional=image->data+nt+24;
    uint32_t rva=get32(optional+168),size=get32(optional+172);
    if(rva!=image->tls_rva || (!!rva!=!!size))return FNV_NATIVE_FORMAT;
    if(!rva)return FNV_NATIVE_OK;
    if(size<24 || (uint64_t)image->base+rva>UINT32_MAX)return FNV_NATIVE_FORMAT;
    const unsigned char *directory=fnv_native_guest_range(image,image->base+rva,size);
    if(!directory)return FNV_NATIVE_FORMAT;
    uint32_t start=get32(directory),end=get32(directory+4),index=get32(directory+8),callbacks=get32(directory+12);
    uint32_t zero=get32(directory+16),flags=get32(directory+20),alignment=(flags>>20)&15;
    if(end<start || (flags&~0x00f00000u) || alignment==15 || !index || (index&3))return FNV_NATIVE_FORMAT;
    uint32_t initialized=end-start;
    if((uint64_t)initialized+zero>FNV_PE_TLS_MAX_BYTES)return FNV_NATIVE_CAPACITY;
    const void *initial=initialized?fnv_native_guest_range(image,start,initialized):NULL;
    unsigned char *index_data=fnv_native_guest_range(image,index,4);
    if((initialized && !initial) || !index_data || index<image->base || index-image->base<image->headers ||
       intersects(index,4,start,initialized) || intersects(index,4,image->base+rva,24) ||
       (callbacks && intersects(index,4,callbacks,4)))return FNV_NATIVE_FORMAT;
    /* Check writable PE section metadata instead of assuming index is data. */
    uint32_t optional_bytes=image->data[nt+20]|((uint32_t)image->data[nt+21]<<8);
    uint64_t table=(uint64_t)nt+24+optional_bytes;int writable=0;
    if(optional_bytes<224 || table+(uint64_t)image->section_count*40>image->headers)return FNV_NATIVE_FORMAT;
    for(unsigned i=0;i<image->section_count;++i){const struct fnv_native_section *s=image->sections+i;
        uint64_t at=(uint64_t)image->base+s->rva;
        if(index>=at && (uint64_t)index+4<=at+s->size && (get32(image->data+table+i*40+36)&0x80000000u))writable=1;
    }
    if(!writable)return FNV_NATIVE_FORMAT;
    if(callbacks){if(callbacks&3)return FNV_NATIVE_FORMAT;
        const unsigned char *first=fnv_native_guest_range(image,callbacks,4);
        if(!first)return FNV_NATIVE_FORMAT;
        if(get32(first))return FNV_NATIVE_UNRESOLVED;
    }
    struct fnv_pe_tls *plan=malloc(sizeof(*plan)+initialized);if(!plan)return FNV_NATIVE_CAPACITY;
    plan->info=(struct fnv_pe_tls_info){initialized,zero,alignment?1u<<(alignment-1):16,index};plan->index=index_data;
    if(initialized)memcpy(plan->initial,initial,initialized);
    *out=plan;return FNV_NATIVE_OK;
}
void fnv_pe_tls_destroy(struct fnv_pe_tls *plan){free(plan);}
void fnv_pe_tls_describe(const struct fnv_pe_tls *plan,struct fnv_pe_tls_info *out){if(out){memset(out,0,sizeof(*out));if(plan)*out=plan->info;}}
void fnv_pe_tls_bind_index(struct fnv_pe_tls *plan){if(plan)memset(plan->index,0,4);}
int fnv_pe_tls_initialize(const struct fnv_pe_tls *plan,void *data,size_t bytes){
    if(!plan || !data || bytes<(size_t)plan->info.initialized_bytes+plan->info.zero_bytes)return 0;
    memcpy(data,plan->initial,plan->info.initialized_bytes);
    memset((unsigned char *)data+plan->info.initialized_bytes,0,plan->info.zero_bytes);return 1;
}
