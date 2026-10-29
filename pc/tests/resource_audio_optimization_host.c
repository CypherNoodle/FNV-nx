#include "resource_lookup.h"
#include "audio_phase.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct object {uintptr_t id;uint64_t payload;};
static uint32_t rng=0x67584u;
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void *reference(struct object *o,unsigned count,uintptr_t id){
    if(id)for(unsigned i=0;i<count;i++)if(o[i].id==id)return o+i;return NULL;
}
int main(void){
    enum {N=2048};struct object *o=calloc(N,sizeof(*o));
    struct fnv_resource_lookup index={.entries=calloc(N*2,sizeof(*index.entries)),.capacity=N*2};assert(o && index.entries);
    /* All IDs collide in the former id%128 direct-mapped cache. */
    for(unsigned i=0;i<N;i++)o[i].id=1+(uintptr_t)i*128;
    for(unsigned pass=0;pass<128;pass++)for(unsigned i=0;i<N;i++)assert(fnv_resource_lookup_find(&index,o[i].id,2,o,sizeof(*o),N)==o+i);
    uint64_t old_steps=(uint64_t)128*N*(N+1)/2;
    assert(index.hits>(uint64_t)N*120 && index.scan_steps<old_steps/32);
    uint64_t warm_hits=index.hits,warm_scans=index.scan_steps;
    /* Wrong-kind requests must fail even for a cached identity. */
    for(unsigned i=0;i<N;i++)assert(!fnv_resource_lookup_find(&index,o[i].id,1,o,sizeof(*o),0));
    free(index.entries);index=(struct fnv_resource_lookup){.entries=calloc(16,sizeof(*index.entries)),.capacity=16};assert(index.entries);
    uintptr_t serial=UINT64_C(0x100000000);
    for(unsigned i=0;i<N;i++)o[i].id=0;
    struct object typed[5][64]={0};
    for(unsigned round=0;round<200000;round++){
        unsigned kind=random32()%5,slot=random32()%64;
        if((random32()&3)==0)typed[kind][slot].id=(random32()&1)?++serial:0;
        uintptr_t id=(random32()&3)?typed[random32()%5][random32()%64].id:serial+random32()%19;
        void *expected=reference(typed[kind],64,id);
        assert(fnv_resource_lookup_find(&index,id,kind,typed[kind],sizeof(struct object),64)==expected);
    }
    /* Compare bulk cursor advancement to the original per-frame algorithm,
     * including short loops, fractional positions and end-of-buffer stops. */
    for(unsigned i=0;i<200000;i++){
        uint64_t end=(1+random32()%8192)*UINT64_C(48000),phase=random32()%end;
        uint32_t frames=1+random32()%512,hz=100+random32()%199901,loop=random32()&1,playing=1,expected_playing=1;
        uint64_t expected=phase;
        for(unsigned f=0;f<frames && expected_playing;f++){
            expected+=hz;
            if(expected>=end){if(loop)expected%=end;else{expected=0;expected_playing=0;}}
        }
        uint64_t actual=fnv_audio_silent_advance(phase,frames,hz,end,loop,&playing);
        assert(actual==expected && playing==expected_playing);
    }
    printf("{\"resource_cases\":464192,\"silent_audio_cases\":200000,\"collision_hits\":%llu,\"scan_steps\":%llu,\"old_collision_scan_steps\":%llu}\n",(unsigned long long)warm_hits,(unsigned long long)warm_scans,(unsigned long long)old_steps);
    free(index.entries);free(o);return 0;
}
