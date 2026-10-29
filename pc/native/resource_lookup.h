#ifndef FNV_RESOURCE_LOOKUP_H
#define FNV_RESOURCE_LOOKUP_H
#include <stddef.h>
#include <stdint.h>
/* Caller serializes access. Arrays and their identity fields remain allocated
 * until the index is discarded. IDs are never reused. Stale entries act as
 * tombstones, so deletion cannot hide a collided entry later in the probe. */
struct fnv_resource_lookup_entry {uintptr_t id;const uintptr_t *identity;void *resource;unsigned kind;};
struct fnv_resource_lookup {
    struct fnv_resource_lookup_entry *entries;
    size_t capacity;
    uint64_t calls,hits,scan_steps;
};
static inline size_t fnv_resource_lookup_hash(uintptr_t id){
    uint64_t x=(uint64_t)id;x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);
    x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);x^=x>>31;return (size_t)x;
}
static inline void *fnv_resource_lookup_find(struct fnv_resource_lookup *index,
        uintptr_t id,unsigned kind,void *base,size_t stride,uint32_t count){
    if(!id || !base)return NULL;
    index->calls++;
    struct fnv_resource_lookup_entry *vacant=NULL;
    size_t start=fnv_resource_lookup_hash(id)&(index->capacity-1);
    /* Bound tombstone/collision cost even after prolonged resource churn. */
    size_t probes=index->capacity<32?index->capacity:32;
    for(size_t n=0;n<probes;++n){
        struct fnv_resource_lookup_entry *e=index->entries+((start+n)&(index->capacity-1));
        if(e->id && e->identity && *e->identity==e->id){
            if(e->id==id){index->hits++;return e->kind==kind?e->resource:NULL;}
        }else if(!vacant)vacant=e;
        if(!e->id)break;
    }
    for(uint32_t i=0;i<count;++i){
        void *resource=(unsigned char*)base+(size_t)i*stride;uintptr_t *identity=resource;
        index->scan_steps++;
        if(*identity==id){
            /* A saturated cache is only an optimization miss. Never change
             * lookup semantics or install a negative result. */
            if(vacant)*vacant=(struct fnv_resource_lookup_entry){id,identity,resource,kind};
            return resource;
        }
    }
    return NULL;
}
#endif
