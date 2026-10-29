#ifndef FNV_SHADER_PAIR_CENSUS_H
#define FNV_SHADER_PAIR_CENSUS_H
#include <stdint.h>
#define FNV_SHADER_PAIR_CENSUS_CAPACITY 32
/* Diagnostic fingerprints only: never authorize or link a program by hash. */
struct fnv_shader_pair_record {
    uint64_t vertex_hash,pixel_hash,hits;
    uint32_t vertex_bytes,pixel_bytes,fixed_bytes,reason;
};
struct fnv_shader_pair_census {
    struct fnv_shader_pair_record rows[FNV_SHADER_PAIR_CENSUS_CAPACITY];
    uint64_t overflow;uint32_t count;
};
static inline uint64_t fnv_shader_fingerprint(const unsigned char *p,uint32_t bytes){
    uint64_t h=UINT64_C(14695981039346656037);
    for(uint32_t i=0;i<bytes;i++){h^=p[i];h*=UINT64_C(1099511628211);}
    return h;
}
static inline void fnv_shader_pair_note(struct fnv_shader_pair_census *c,
    uint64_t vs,uint32_t vb,uint64_t ps,uint32_t pb,uint32_t fixed,uint32_t reason){
    for(uint32_t i=0;i<c->count;i++){
        struct fnv_shader_pair_record *r=c->rows+i;
        if(r->vertex_hash==vs && r->pixel_hash==ps && r->vertex_bytes==vb &&
           r->pixel_bytes==pb && r->fixed_bytes==fixed && r->reason==reason){r->hits++;return;}
    }
    if(c->count==FNV_SHADER_PAIR_CENSUS_CAPACITY){c->overflow++;return;}
    c->rows[c->count++]=(struct fnv_shader_pair_record){vs,ps,1,vb,pb,fixed,reason};
}
#endif
