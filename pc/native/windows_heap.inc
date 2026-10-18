/* Fixed-budget process heap and VM-backed private heaps. Included by windows_thread.c; original code.
 * Ordered host metadata is never exposed in the guest allocation payload.
 * No guest pointer validation/diagnostic may exit while heap_guard is held. */
static int heap_overlap(struct fnv_windows_process *p,uint32_t at,uint32_t bytes){
    return p->config.heap_bytes && overlap(at,bytes,p->config.heap_address,p->config.heap_bytes);
}
static enum fnv_cpu_result heap_prepare(struct fnv_windows_process *p){
    const struct fnv_windows_config *c=&p->config;
    if((!c->private_heap_reserve)!=(!c->private_heap_limit) ||
       (c->private_heap_reserve && (!c->memory || (c->private_heap_reserve&4095) ||
        c->private_heap_limit>1024 || !c->heap_bytes)))return FNV_CPU_INVALID;
    if(!c->heap_bytes)return (c->heap_address || c->heap_capacity)?FNV_CPU_INVALID:FNV_CPU_OK;
    if(!c->heap_address || (c->heap_address&7) || (c->heap_bytes&7) || c->heap_bytes<16 ||
       !c->heap_capacity || c->heap_capacity>1048576 || !inside(c,c->heap_address,c->heap_bytes) ||
       (uint64_t)c->heap_address+c->heap_bytes>UINT32_MAX ||
       overlap(c->heap_address,c->heap_bytes,c->cpu.guest_base,c->image_bytes) ||
       overlap(c->heap_address,c->heap_bytes,c->peb_address,4096) || pool_overlap(p,c->heap_address,c->heap_bytes) ||
       read32(memory(p,c->peb_address)+0x18))return FNV_CPU_INVALID;
    uint32_t initial=c->heap_capacity<32?c->heap_capacity:32;
    struct heap_block *blocks=malloc(initial*sizeof(*blocks));
    if(!blocks)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&p->heap_guard)){free(blocks);return FNV_CPU_NO_MEMORY;}
    p->heap=(struct heap_store){.blocks=blocks,.capacity=initial,.address=c->heap_address,.bytes=c->heap_bytes,.committed=c->heap_bytes};
    write32(memory(p,c->peb_address)+0x18,c->heap_address);return FNV_CPU_OK;
}
static struct fnv_windows_process *heap_process(uint32_t flags,uint32_t allowed){
    struct fnv_windows_process *p=thread()->process;
    if(!p->heap.blocks || (flags&~allowed)){
        recomp_unimpl(!p->heap.blocks?"Windows process heap unavailable":"Windows heap flags/SEH pending",flags);abort();
    }
    return p;
}
static struct fnv_windows_process *heap_access(uint32_t handle,uint32_t flags,uint32_t allowed){
    struct fnv_windows_process *p=heap_process(flags,allowed|1u);
    if((flags&1) && handle==p->config.heap_address){
        recomp_unimpl("Windows process heap NO_SERIALIZE is unsupported",flags);abort();
    }
    return p;
}
static uint32_t heap_find(struct heap_store *h,uint32_t at){
    uint32_t low=0,high=h->count;
    while(low<high){uint32_t middle=low+(high-low)/2;h->find_steps++;
        if(h->blocks[middle].address==at)return middle;
        if(h->blocks[middle].address<at)low=middle+1;else high=middle;
    }
    return UINT32_MAX;
}
static uint32_t heap_span(uint32_t bytes){
    return bytes>UINT32_MAX-7?0:bytes?((bytes+7)&~7u):8;
}
static struct heap_store *heap_lookup(struct fnv_windows_process *p,uint32_t handle){
    if(handle==p->heap.address)return &p->heap;
    for(struct heap_store *h=p->private_heaps;h;h=h->next)if(h->address==handle)return h;
    return NULL;
}
enum fnv_cpu_result fnv_windows_heap_stats(struct fnv_windows_process *p,struct fnv_windows_heap_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_heap_stats s={0};
    if(p->heap.blocks){fnv_host_mutex_lock(&p->heap_guard);
        for(struct heap_store *h=&p->heap;h;h=h==&p->heap?p->private_heaps:h->next){
            s.live_heaps++;s.live_blocks+=h->count;s.metadata_bytes+=(uint64_t)h->capacity*sizeof(*h->blocks);
            s.lookup_steps+=h->find_steps;s.allocation_steps+=h->allocation_steps;s.gap_steps+=h->gap_steps;
        }
        fnv_host_mutex_unlock(&p->heap_guard);}
    *out=s;return FNV_CPU_OK;
}
static int heap_metadata_grow(struct fnv_windows_process *p,struct heap_store *h){
    if(h->count<h->capacity)return 1;
    uint32_t next=h->capacity?h->capacity*2:32;
    if(next>p->config.heap_capacity)next=p->config.heap_capacity;
    if(next<=h->capacity)return 0;
    struct heap_block *blocks=realloc(h->blocks,(size_t)next*sizeof(*blocks));
    if(!blocks)return 0;
    h->blocks=blocks;h->capacity=next;return 1;
}
/* Earliest nonempty address gap, independent of the next request's size.
 * Never skip a smaller hole merely because a larger allocation did not fit. */
static void heap_gap_advance(struct heap_store *h){
    while(h->first_gap<h->count){uint32_t i=h->first_gap;h->gap_steps++;
        uint64_t at=i?(uint64_t)h->blocks[i-1].address+h->blocks[i-1].span:(uint64_t)h->address+8;
        if(at<h->blocks[i].address)break;
        h->first_gap++;
    }
}
static void heap_vm_fault(struct fnv_windows_process *p,struct fnv_vm_result r){
    if(r.error==FNV_VM_CORRUPT)p->heap_native_fault=r.native_error?r.native_error:UINT32_MAX;
}
static void heap_return(uint32_t value,uint32_t argc,uint32_t fault){
    if(fault){recomp_unimpl("Windows heap VM permission rollback failed",fault);abort();}
    fnv_cpu_return_u32(value,argc);
}
static uint32_t heap_commit_locked(struct fnv_windows_process *p,struct heap_store *h,uint64_t end){
    uint32_t used=(uint32_t)(end-h->address);
    if(used<=h->committed)return 1;
    uint64_t target=((uint64_t)used+4095)&~UINT64_C(4095);
    if(target>h->bytes)return 0;
    struct fnv_vm_result r=fnv_vm_allocate(p->config.memory,h->address+h->committed,(uint32_t)target-h->committed,FNV_VM_COMMIT,FNV_VM_READWRITE);
    heap_vm_fault(p,r);if(r.error)return 0;
    h->committed=(uint32_t)target;return 1;
}
static uint32_t heap_allocate_locked(struct fnv_windows_process *p,struct heap_store *h,uint32_t bytes,uint32_t flags){
    uint32_t span=heap_span(bytes),i=h->first_gap;
    uint64_t at=i?(uint64_t)h->blocks[i-1].address+h->blocks[i-1].span:(uint64_t)h->address+8;
    if(p->heap_native_fault || !span || (h->fixed && bytes>=0x7fff8u) || h->count==p->config.heap_capacity)return 0;
    for(;i<h->count;++i){struct heap_block *b=h->blocks+i;h->allocation_steps++;
        if((uint64_t)at+span<=b->address)break;
        at=(uint64_t)b->address+b->span;
    }
    if((uint64_t)at+span>(uint64_t)h->address+h->bytes || !heap_metadata_grow(p,h) || !heap_commit_locked(p,h,(uint64_t)at+span))return 0;
    memmove(h->blocks+i+1,h->blocks+i,(h->count-i)*sizeof(*h->blocks));
    h->blocks[i]=(struct heap_block){(uint32_t)at,bytes,span};h->count++;
    if(i==h->first_gap){h->first_gap=i+1;heap_gap_advance(h);}
    if(flags&8)memset(memory(p,(uint32_t)at),0,bytes);
    return (uint32_t)at;
}
static void heap_remove_locked(struct heap_store *h,uint32_t i){
    memmove(h->blocks+i,h->blocks+i+1,(h->count-i-1)*sizeof(*h->blocks));h->count--;
    if(i<h->first_gap)h->first_gap=i;
}
static void api_process_heap(void){
    struct fnv_windows_process *p=heap_process(0,0);fnv_cpu_return_u32(p->config.heap_address,0);
}
static void api_heap_alloc(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_access(handle,flags,8);uint32_t value=0;
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store *h=heap_lookup(p,handle);
    if(h)value=heap_allocate_locked(p,h,bytes,flags);
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);heap_return(value,12,fault);
}
static void api_heap_realloc(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2),bytes=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=heap_access(handle,flags,8|16);uint32_t value=0,span=heap_span(bytes);
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store *h=heap_lookup(p,handle);uint32_t i=h?heap_find(h,at):UINT32_MAX;
    if(!p->heap_native_fault && i!=UINT32_MAX && span && !(h->fixed && bytes>=0x7fff8u)){
        struct heap_block old=h->blocks[i];uint64_t end=i+1<h->count?h->blocks[i+1].address:(uint64_t)h->address+h->bytes;
        if((uint64_t)at+span<=end){
            if(heap_commit_locked(p,h,(uint64_t)at+span)){
                if((flags&8) && bytes>old.bytes)memset(memory(p,at)+old.bytes,0,bytes-old.bytes);
                h->blocks[i].bytes=bytes;h->blocks[i].span=span;value=at;
                if(span<old.span && h->first_gap>i+1)h->first_gap=i+1;
                else if(span>old.span && h->first_gap==i+1)heap_gap_advance(h);
            }
        }else if(!(flags&16)){
            value=heap_allocate_locked(p,h,bytes,flags);
            if(value){memcpy(memory(p,value),memory(p,at),bytes<old.bytes?bytes:old.bytes);heap_remove_locked(h,heap_find(h,at));}
        }
    }
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);heap_return(value,16,fault);
}
static void api_heap_free(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_access(handle,flags,0);uint32_t value=0;
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store *h=heap_lookup(p,handle);uint32_t i=h?heap_find(h,at):UINT32_MAX;
    if(!p->heap_native_fault && h){if(!at)value=1;else if(i!=UINT32_MAX){heap_remove_locked(h,i);value=1;}}
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);if(!value)fnv_windows_set_last_error(87);heap_return(value,12,fault);
}
static void api_heap_size(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_access(handle,flags,0);uint32_t value=UINT32_MAX;
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store *h=heap_lookup(p,handle);uint32_t i=h?heap_find(h,at):UINT32_MAX;
    if(!p->heap_native_fault && i!=UINT32_MAX)value=h->blocks[i].bytes;
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);if(value==UINT32_MAX)fnv_windows_set_last_error(87);heap_return(value,12,fault);
}
static void api_heap_validate(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_access(handle,flags,0);
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store *h=heap_lookup(p,handle);uint32_t value=!p->heap_native_fault && h && (!at || heap_find(h,at)!=UINT32_MAX);
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);heap_return(value,12,fault);
}
static void api_heap_create(void){
    uint32_t flags=fnv_cpu_argument_u32(0),initial=fnv_cpu_argument_u32(1),maximum=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=heap_process(flags,1);uint32_t value=0,error=8;
    if(!p->config.private_heap_reserve){recomp_unimpl("Windows private heap provider unavailable",0);abort();}
    uint64_t reserved=maximum?((uint64_t)maximum+4095)&~UINT64_C(4095):p->config.private_heap_reserve;
    uint64_t committed=initial?((uint64_t)initial+4095)&~UINT64_C(4095):4096;
    fnv_host_mutex_lock(&p->heap_guard);
    if(!p->heap_native_fault && reserved<=p->config.private_heap_reserve && committed<=reserved && p->private_heap_count<p->config.private_heap_limit){
        struct heap_store *h=calloc(1,sizeof(*h));
        if(h){struct fnv_vm_result r=fnv_vm_allocate(p->config.memory,0,(uint32_t)reserved,FNV_VM_RESERVE,FNV_VM_READWRITE);heap_vm_fault(p,r);
                if(!r.error){h->address=r.value;h->bytes=(uint32_t)reserved;h->fixed=maximum!=0;h->options=flags;
                    r=fnv_vm_allocate(p->config.memory,h->address,(uint32_t)committed,FNV_VM_COMMIT,FNV_VM_READWRITE);heap_vm_fault(p,r);
                    if(!r.error){h->committed=(uint32_t)committed;h->next=p->private_heaps;p->private_heaps=h;p->private_heap_count++;value=h->address;h=NULL;}
                    else {r=fnv_vm_free(p->config.memory,h->address,0,FNV_VM_RELEASE);heap_vm_fault(p,r);
                        /* Retain the reservation on rollback failure for diagnostics, never publish it. */
                        if(r.error)p->heap_native_fault=r.native_error?r.native_error:r.error;}
                }
            if(h){free(h->blocks);free(h);}
        }
    }
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);if(!value)fnv_windows_set_last_error(error);heap_return(value,12,fault);
}
static void api_heap_destroy(void){
    uint32_t handle=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=heap_process(0,0);uint32_t value=0,error=87;
    fnv_host_mutex_lock(&p->heap_guard);struct heap_store **link=&p->private_heaps;
    while(*link && (*link)->address!=handle)link=&(*link)->next;
    if(!p->heap_native_fault && *link){struct heap_store *h=*link;struct fnv_vm_result r=fnv_vm_free(p->config.memory,h->address,0,FNV_VM_RELEASE);heap_vm_fault(p,r);
        if(!r.error){*link=h->next;p->private_heap_count--;free(h->blocks);free(h);value=1;}else error=r.error;}
    uint32_t fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);if(!value)fnv_windows_set_last_error(error);heap_return(value,4,fault);
}
