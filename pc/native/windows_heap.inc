/* Fixed-budget process heap. Included by windows_thread.c; original code.
 * Ordered host metadata is never exposed in the guest allocation payload.
 * No guest pointer validation/diagnostic may exit while heap_guard is held. */
static int heap_overlap(struct fnv_windows_process *p,uint32_t at,uint32_t bytes){
    return p->config.heap_bytes && overlap(at,bytes,p->config.heap_address,p->config.heap_bytes);
}
static enum fnv_cpu_result heap_prepare(struct fnv_windows_process *p){
    const struct fnv_windows_config *c=&p->config;
    if(!c->heap_bytes)return (c->heap_address || c->heap_capacity)?FNV_CPU_INVALID:FNV_CPU_OK;
    if(!c->heap_address || (c->heap_address&7) || (c->heap_bytes&7) || c->heap_bytes<16 ||
       !c->heap_capacity || c->heap_capacity>1048576 || !inside(c,c->heap_address,c->heap_bytes) ||
       (uint64_t)c->heap_address+c->heap_bytes>UINT32_MAX ||
       overlap(c->heap_address,c->heap_bytes,c->cpu.guest_base,c->image_bytes) ||
       overlap(c->heap_address,c->heap_bytes,c->peb_address,4096) || pool_overlap(p,c->heap_address,c->heap_bytes) ||
       read32(memory(p,c->peb_address)+0x18))return FNV_CPU_INVALID;
    struct heap_block *blocks=calloc(c->heap_capacity,sizeof(*blocks));
    if(!blocks)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&p->heap_guard)){free(blocks);return FNV_CPU_NO_MEMORY;}
    p->heap_blocks=blocks;
    write32(memory(p,c->peb_address)+0x18,c->heap_address);return FNV_CPU_OK;
}
static struct fnv_windows_process *heap_process(uint32_t flags,uint32_t allowed){
    struct fnv_windows_process *p=thread()->process;
    if(!p->heap_blocks || (flags&~allowed)){
        recomp_unimpl(!p->heap_blocks?"Windows process heap unavailable":"Windows heap flags/SEH pending",flags);abort();
    }
    return p;
}
static uint32_t heap_find(struct fnv_windows_process *p,uint32_t at){
    for(uint32_t i=0;i<p->heap_count;++i)if(p->heap_blocks[i].address==at)return i;
    return UINT32_MAX;
}
static uint32_t heap_span(uint32_t bytes){
    return bytes>UINT32_MAX-7?0:bytes?((bytes+7)&~7u):8;
}
static uint32_t heap_allocate_locked(struct fnv_windows_process *p,uint32_t bytes,uint32_t flags){
    uint32_t span=heap_span(bytes),at=p->config.heap_address+8,i=0;
    if(!span || p->heap_count==p->config.heap_capacity)return 0;
    for(;i<p->heap_count;++i){struct heap_block *b=p->heap_blocks+i;
        if((uint64_t)at+span<=b->address)break;
        at=b->address+b->span;
    }
    if((uint64_t)at+span>(uint64_t)p->config.heap_address+p->config.heap_bytes)return 0;
    memmove(p->heap_blocks+i+1,p->heap_blocks+i,(p->heap_count-i)*sizeof(*p->heap_blocks));
    p->heap_blocks[i]=(struct heap_block){at,bytes,span};p->heap_count++;
    if(flags&8)memset(memory(p,at),0,bytes);
    return at;
}
static void heap_remove_locked(struct fnv_windows_process *p,uint32_t i){
    memmove(p->heap_blocks+i,p->heap_blocks+i+1,(p->heap_count-i-1)*sizeof(*p->heap_blocks));p->heap_count--;
}
static void api_process_heap(void){
    struct fnv_windows_process *p=heap_process(0,0);fnv_cpu_return_u32(p->config.heap_address,0);
}
static void api_heap_alloc(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_process(flags,8);uint32_t value=0;
    fnv_host_mutex_lock(&p->heap_guard);
    if(handle==p->config.heap_address)value=heap_allocate_locked(p,bytes,flags);
    fnv_host_mutex_unlock(&p->heap_guard);fnv_cpu_return_u32(value,12);
}
static void api_heap_realloc(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2),bytes=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=heap_process(flags,8|16);uint32_t value=0,span=heap_span(bytes);
    fnv_host_mutex_lock(&p->heap_guard);uint32_t i=heap_find(p,at);
    if(handle==p->config.heap_address && i!=UINT32_MAX && span){
        struct heap_block old=p->heap_blocks[i];uint32_t end=i+1<p->heap_count?p->heap_blocks[i+1].address:p->config.heap_address+p->config.heap_bytes;
        if((uint64_t)at+span<=end){
            if((flags&8) && bytes>old.bytes)memset(memory(p,at)+old.bytes,0,bytes-old.bytes);
            p->heap_blocks[i].bytes=bytes;p->heap_blocks[i].span=span;value=at;
        }else if(!(flags&16)){
            value=heap_allocate_locked(p,bytes,flags);
            if(value){memcpy(memory(p,value),memory(p,at),bytes<old.bytes?bytes:old.bytes);heap_remove_locked(p,heap_find(p,at));}
        }
    }
    fnv_host_mutex_unlock(&p->heap_guard);fnv_cpu_return_u32(value,16);
}
static void api_heap_free(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_process(flags,0);uint32_t value=0;
    fnv_host_mutex_lock(&p->heap_guard);uint32_t i=heap_find(p,at);
    if(handle==p->config.heap_address){if(!at)value=1;else if(i!=UINT32_MAX){heap_remove_locked(p,i);value=1;}}
    fnv_host_mutex_unlock(&p->heap_guard);if(!value)fnv_windows_set_last_error(87);fnv_cpu_return_u32(value,12);
}
static void api_heap_size(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_process(flags,0);uint32_t value=UINT32_MAX;
    fnv_host_mutex_lock(&p->heap_guard);uint32_t i=heap_find(p,at);
    if(handle==p->config.heap_address && i!=UINT32_MAX)value=p->heap_blocks[i].bytes;
    fnv_host_mutex_unlock(&p->heap_guard);if(value==UINT32_MAX)fnv_windows_set_last_error(87);fnv_cpu_return_u32(value,12);
}
static void api_heap_validate(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_process(flags,0);
    fnv_host_mutex_lock(&p->heap_guard);uint32_t value=handle==p->config.heap_address && (!at || heap_find(p,at)!=UINT32_MAX);
    fnv_host_mutex_unlock(&p->heap_guard);fnv_cpu_return_u32(value,12);
}
