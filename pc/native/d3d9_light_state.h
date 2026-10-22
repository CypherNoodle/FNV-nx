#ifndef FNV_D3D9_LIGHT_STATE_H
#define FNV_D3D9_LIGHT_STATE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* CPU storage only, externally serialized. Sparse arbitrary DWORD indices.
 * Bounded allocation budget, not a claim about renderer/active-light caps. */
struct fnv_light {uint32_t index,words[26],enabled,mask;};
struct fnv_light_bank {struct fnv_light *entries;uint32_t count,capacity;};
static inline uint32_t fnv_light_position(const struct fnv_light_bank *b,uint32_t id){
    uint32_t lo=0,hi=b->count;while(lo<hi){uint32_t m=lo+(hi-lo)/2;if(b->entries[m].index<id)lo=m+1;else hi=m;}return lo;
}
static inline struct fnv_light *fnv_light_find(const struct fnv_light_bank *b,uint32_t id){
    uint32_t p=fnv_light_position(b,id);return p<b->count && b->entries[p].index==id?b->entries+p:NULL;
}
static inline uint32_t fnv_light_reserve(struct fnv_light_bank *b,uint32_t n){
    if(n<=b->capacity)return 0;
    if(n>4096)return 0x8876017cu;
    uint32_t size=b->capacity?b->capacity:8;while(size<n)size*=2;
    void *p=realloc(b->entries,(size_t)size*sizeof(*b->entries));if(!p)return 0x8876017cu;
    b->entries=p;b->capacity=size;return 0;
}
static inline struct fnv_light *fnv_light_insert(struct fnv_light_bank *b,uint32_t id){
    struct fnv_light *found=fnv_light_find(b,id);if(found)return found;
    uint32_t p=fnv_light_position(b,id);memmove(b->entries+p+1,b->entries+p,(b->count-p)*sizeof(*b->entries));
    struct fnv_light v={.index=id};v.words[0]=3;v.words[1]=v.words[2]=v.words[3]=v.words[18]=0x3f800000;
    b->entries[p]=v;b->count++;return b->entries+p;
}
static inline uint32_t fnv_light_change(struct fnv_light_bank *live,struct fnv_light_bank *record,uint32_t id,const uint32_t *words,const uint32_t *enabled){
    /* Reserve every destination before publishing a new live default. */
    uint32_t hr=fnv_light_reserve(live,live->count+(fnv_light_find(live,id)==NULL));if(hr)return hr;
    if(record){hr=fnv_light_reserve(record,record->count+(fnv_light_find(record,id)==NULL));if(hr)return hr;}
    struct fnv_light *l=fnv_light_insert(live,id),*target=record?fnv_light_insert(record,id):l;
    if(words){memmove(target->words,words,104);target->mask|=1;}
    if(enabled){target->enabled=*enabled?128:0;target->mask|=2;}
    return 0;
}
static inline uint32_t fnv_light_block_prepare(struct fnv_light_bank *live,const struct fnv_light_bank *record,int capture){
    uint32_t missing=0;for(uint32_t i=0;i<record->count;i++)if(!fnv_light_find(live,record->entries[i].index))missing++;
    if(capture)return missing?0x8876086cu:0;
    return fnv_light_reserve(live,live->count+missing);
}
/* Call only after successful prepare and the rest of the state transaction. */
static inline void fnv_light_block_commit(struct fnv_light_bank *live,struct fnv_light_bank *record,int capture){
    for(uint32_t i=0;i<record->count;i++){
        struct fnv_light *r=record->entries+i,*l=fnv_light_insert(live,r->index);
        struct fnv_light *dst=capture?r:l,*src=capture?l:r;
        if(r->mask&1)memcpy(dst->words,src->words,104);
        if(r->mask&2)dst->enabled=src->enabled;
    }
}
static inline void fnv_light_destroy(struct fnv_light_bank *b){free(b->entries);memset(b,0,sizeof(*b));}
#endif
