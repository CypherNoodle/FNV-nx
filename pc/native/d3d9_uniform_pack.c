#include "d3d9_uniform_pack.h"
#include <stddef.h>
#include <string.h>
#define INVALID 0x8876086Cu

static int overlaps(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t av=(uintptr_t)a,bv=(uintptr_t)b;
    if(!an || !bn)return 0;
    if(av>UINTPTR_MAX-an || bv>UINTPTR_MAX-bn)return 1;
    return av<bv+bn && bv<av+an;
}

uint32_t fnv_d3d9_uniform_pack(const struct fnv_uniform_plan *p,
    const struct fnv_uniform_banks *banks, void *destination, uint32_t capacity) {
    if(!p || !banks || p->bytes>65536 || p->bytes%16 || p->count>512 ||
        capacity<p->bytes || (p->bytes && !destination) || (p->count && !p->entries))return INVALID;
    if(overlaps(destination,p->bytes,p,sizeof(*p)) ||
       overlaps(destination,p->bytes,banks,sizeof(*banks)) ||
       overlaps(destination,p->bytes,p->entries,(size_t)p->count*sizeof(*p->entries)))return INVALID;
    for(uint32_t k=0;k<3;++k){
        if(banks->count[k]>(k?16u:256u))return INVALID;
        size_t size=(size_t)banks->count[k]*(k==2?4u:16u);
        if(size && !banks->bits[k])return INVALID;
        if(overlaps(destination,p->bytes,banks->bits[k],size))return INVALID;
    }
    for(uint32_t i=0;i<p->count;++i){
        const struct fnv_uniform_entry *e=p->entries+i;
        if(e->kind>2 || !e->count || e->stride!=16 || e->offset%16 ||
           e->first>banks->count[e->kind] || e->count>banks->count[e->kind]-e->first ||
           e->offset>p->bytes || e->count>(p->bytes-e->offset)/16)return INVALID;
        for(uint32_t j=0;j<i;++j){
            const struct fnv_uniform_entry *other=p->entries+j;
            if(e->offset<other->offset+other->count*16 && other->offset<e->offset+e->count*16)return INVALID;
        }
    }
    if(p->bytes)memset(destination,0,p->bytes);
    for(uint32_t i=0;i<p->count;++i){
        const struct fnv_uniform_entry *e=p->entries+i;
        unsigned char *dst=(unsigned char*)destination+e->offset;
        if(e->kind!=2){
            memcpy(dst,banks->bits[e->kind]+e->first*4,(size_t)e->count*16);
        }else for(uint32_t j=0;j<e->count;++j){
            uint32_t boolean=banks->bits[2][e->first+j]!=0;
            memcpy(dst+j*16,&boolean,4);
        }
    }
    return 0;
}
