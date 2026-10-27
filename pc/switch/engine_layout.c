#include "engine_layout.h"
#include <stddef.h>

int fnv_engine_layout_valid(const struct fnv_engine_layout *p) {
    if(!p || !p->base || (p->base&65535) || !p->arena_bytes ||
       (p->arena_bytes&65535) || (uint64_t)p->base+p->arena_bytes>UINT64_C(0x100000000))return 0;
    for(unsigned i=0;i<FNV_ENGINE_REGION_COUNT;i++) {
        const struct fnv_engine_region *r=p->regions+i;
        if(!r->bytes || (r->offset&4095) || (r->bytes&4095) ||
           (uint64_t)r->offset+r->bytes>p->arena_bytes)return 0;
        for(unsigned j=0;j<i;j++) {
            const struct fnv_engine_region *q=p->regions+j;
            if((uint64_t)r->offset<((uint64_t)q->offset+q->bytes) &&
               (uint64_t)q->offset<((uint64_t)r->offset+r->bytes))return 0;
        }
    }
    if(p->regions[FNV_ENGINE_IMAGE].offset || !p->worker_capacity || p->worker_capacity>32 ||
       !p->worker_stack_bytes || (p->worker_stack_bytes&4095) ||
       !p->worker_tls_bytes || (p->worker_tls_bytes&4095) ||
       p->worker_host_stack_bytes<65536 || (p->worker_host_stack_bytes&4095) ||
       !p->commit_limit || (p->commit_limit&65535) ||
       p->commit_limit>p->regions[FNV_ENGINE_VM].bytes)return 0;
    uint64_t stride=UINT64_C(8192)+p->worker_stack_bytes+p->worker_tls_bytes;
    return stride*p->worker_capacity<=p->regions[FNV_ENGINE_WORKERS].bytes;
}
int fnv_engine_layout_create(uint32_t base,uint32_t image_bytes,struct fnv_engine_layout *out) {
    if(!out || !image_bytes || (image_bytes&4095))return 0;
    struct fnv_engine_layout p={
        .base=base,.arena_bytes=808u<<20,.commit_limit=760u<<20,
        .worker_capacity=32,.worker_stack_bytes=0x20000,.worker_tls_bytes=0x11000,
        .worker_host_stack_bytes=0x100000,
        .regions={
            [FNV_ENGINE_IMAGE]={0,image_bytes},
            [FNV_ENGINE_PEB]={0x2000000,4096},
            [FNV_ENGINE_PARAMETERS]={0x2100000,65536},
            [FNV_ENGINE_MAIN_THREAD]={0x2200000,0x40000},
            [FNV_ENGINE_HEAP]={0x2400000,0x400000},
            [FNV_ENGINE_WORKERS]={0x2800000,0x660000},
            [FNV_ENGINE_VM]={0x3000000,760u<<20}
        }
    };
    if(!fnv_engine_layout_valid(&p))return 0;
    *out=p;return 1;
}
