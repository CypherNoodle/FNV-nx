#ifndef FNV_SHADER_PAIR_CENSUS_H
#define FNV_SHADER_PAIR_CENSUS_H
#include <stdint.h>
#define FNV_SHADER_PAIR_CENSUS_CAPACITY 32
/* Diagnostic fingerprints only: never authorize or link a program by hash. */
struct fnv_shader_pair_record {
    uint64_t vertex_hash,pixel_hash,hits;
    uint32_t vertex_bytes,pixel_bytes,fixed_bytes,reason;
    uint32_t attribute_state,attribute_count,attributes[16];
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
static inline struct fnv_shader_pair_record *fnv_shader_pair_note(struct fnv_shader_pair_census *c,
    uint64_t vs,uint32_t vb,uint64_t ps,uint32_t pb,uint32_t fixed,uint32_t reason){
    for(uint32_t i=0;i<c->count;i++){
        struct fnv_shader_pair_record *r=c->rows+i;
        if(r->vertex_hash==vs && r->pixel_hash==ps && r->vertex_bytes==vb &&
           r->pixel_bytes==pb && r->fixed_bytes==fixed && r->reason==reason){r->hits++;return r;}
    }
    if(c->count==FNV_SHADER_PAIR_CENSUS_CAPACITY){c->overflow++;return 0;}
    struct fnv_shader_pair_record *r=c->rows+c->count++;
    *r=(struct fnv_shader_pair_record){.vertex_hash=vs,.pixel_hash=ps,.hits=1,
        .vertex_bytes=vb,.pixel_bytes=pb,.fixed_bytes=fixed,.reason=reason};return r;
}
/* Capture only actual semantic/index/type triples, not guessed input formats.
 * State 1 is usable, 2 exceeds the offline adapter limit, 3 changed in-session. */
static inline void fnv_shader_pair_attributes(struct fnv_shader_pair_record *r,
    const unsigned char *elements,uint32_t count){
    if(!r)return;
    if(!count || count>16){r->attribute_state=2;return;}
    if(r->attribute_state==2 || r->attribute_state==3)return;
    for(uint32_t i=0;i<count;i++){
        uint32_t packed=elements[i*8+6]|((uint32_t)elements[i*8+7]<<8)|((uint32_t)elements[i*8+4]<<16);
        if(r->attribute_state && (count!=r->attribute_count || packed!=r->attributes[i])){r->attribute_state=3;return;}
        if(!r->attribute_state)r->attributes[i]=packed;
    }
    r->attribute_count=count;r->attribute_state=1;
}
#endif
