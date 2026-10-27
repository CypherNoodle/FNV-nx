#include "engine_layout.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    struct fnv_engine_layout p,bad,untouched;
    memset(&untouched,0xa5,sizeof(untouched));p=untouched;
    assert(!fnv_engine_layout_create(0x400000,0,&p));assert(!memcmp(&p,&untouched,sizeof(p)));
    assert(!fnv_engine_layout_create(0xffff0000,0x100000,&p));assert(!memcmp(&p,&untouched,sizeof(p)));
    assert(!fnv_engine_layout_create(0x400001,0x100000,&p));
    assert(!fnv_engine_layout_create(0x400000,0x2001000,&p));
    assert(!fnv_engine_layout_create(0x400000,0x100001,&p));
    assert(!fnv_engine_layout_create(0x400000,0x100000,NULL));
    assert(fnv_engine_layout_create(0x400000,0x1080000,&p));
    assert(p.arena_bytes==808u<<20 && p.commit_limit==760u<<20);
    assert(p.regions[FNV_ENGINE_VM].offset==48u<<20);
    assert(p.regions[FNV_ENGINE_VM].offset+p.regions[FNV_ENGINE_VM].bytes==p.arena_bytes);
    assert(p.worker_capacity==32 && p.regions[FNV_ENGINE_WORKERS].bytes==32*(8192+p.worker_stack_bytes+p.worker_tls_bytes));
    for(unsigned i=1;i<FNV_ENGINE_REGION_COUNT;i++) {
        bad=p;bad.regions[i].offset=0;assert(!fnv_engine_layout_valid(&bad));
        bad=p;bad.regions[i].bytes=UINT32_MAX;assert(!fnv_engine_layout_valid(&bad));
        bad=p;bad.regions[i].bytes=0;assert(!fnv_engine_layout_valid(&bad));
    }
    bad=p;bad.regions[FNV_ENGINE_VM].offset=40u<<20;assert(!fnv_engine_layout_valid(&bad));
    bad=p;bad.worker_capacity=33;assert(!fnv_engine_layout_valid(&bad));
    bad=p;bad.worker_tls_bytes+=4096;assert(!fnv_engine_layout_valid(&bad));
    bad=p;bad.commit_limit+=65536;assert(!fnv_engine_layout_valid(&bad));
    bad=p;bad.worker_stack_bytes=UINT32_MAX-4095;assert(!fnv_engine_layout_valid(&bad));
    assert(!fnv_engine_layout_valid(NULL));
    puts("PASS engine layout: disjoint regions, worker stride, overflow, unchanged failure output");
    return 0;
}
