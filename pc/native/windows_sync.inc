/* Original guest32 synchronization. Native metadata/conditions stay on host.
 * Guest CS layout is an opaque 24-byte ABI mirror, not a Windows lock algorithm.
 * Caller keeps the CS storage valid and writable through deletion. */
static enum fnv_cpu_result sync_prepare(struct fnv_windows_process *p){
    uint32_t count=p->config.critical_section_capacity;
    if(!count)return FNV_CPU_OK;
    if(count>65536)return FNV_CPU_INVALID;
    struct critical_section *items=calloc(count,sizeof(*items));if(!items)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&p->sync_guard)){free(items);return FNV_CPU_NO_MEMORY;}
    for(uint32_t i=0;i<count;++i)if(!fnv_host_condition_init(&items[i].condition)){
        while(i)fnv_host_condition_destroy(&items[--i].condition);
        fnv_host_mutex_destroy(&p->sync_guard);free(items);return FNV_CPU_NO_MEMORY;
    }
    p->sections=items;return FNV_CPU_OK;
}
static void sync_dispose(struct fnv_windows_process *p){
    if(!p->sections)return;
    for(uint32_t i=0;i<p->config.critical_section_capacity;++i)fnv_host_condition_destroy(&p->sections[i].condition);
    free(p->sections);fnv_host_mutex_destroy(&p->sync_guard);
}
static int sync_thread_idle(struct fnv_windows_process *p,uint32_t id){
    if(!p->sections || fnv_cpu_is_faulted(p->cpu))return 1;
    fnv_host_mutex_lock(&p->sync_guard);int idle=1;
    for(uint32_t i=0;i<p->config.critical_section_capacity;++i)if(p->sections[i].owner==id){idle=0;break;}
    fnv_host_mutex_unlock(&p->sync_guard);return idle;
}
enum fnv_cpu_result fnv_windows_sync_stats(struct fnv_windows_process *p,struct fnv_windows_sync_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_sync_stats s={0};
    if(p->sections){fnv_host_mutex_lock(&p->sync_guard);
        for(uint32_t i=0;i<p->config.critical_section_capacity;++i){struct critical_section *c=p->sections+i;
            s.live_sections+=c->address!=0;s.owned_sections+=c->owner!=0;s.waiting_threads+=c->waiters;}
        s.blocking_waits=p->sync_waits;s.wakeups=p->sync_wakes;s.try_failures=p->sync_try_failures;
        fnv_host_mutex_unlock(&p->sync_guard);}
    *out=s;return FNV_CPU_OK;
}
static void sync_invalid(const char *text,uint32_t address){recomp_unimpl(text,address);abort();}
static unsigned char *sync_pointer(struct fnv_windows_process *p,uint32_t address,uint32_t bytes){
    if(!address || (address&3) || !inside(&p->config,address,bytes))sync_invalid("Windows sync pointer unavailable",address);
    if(pool_overlap(p,address,bytes) && !fnv_vm_access(p->config.memory,address,bytes,1))sync_invalid("Windows sync pointer not writable",address);
    return memory(p,address);
}
static struct critical_section *cs_find(struct fnv_windows_process *p,uint32_t address){
    for(uint32_t i=0;i<p->config.critical_section_capacity;++i)if(p->sections[i].address==address)return p->sections+i;
    return NULL;
}
static int cs_mirror(struct critical_section *c,unsigned char *m,int write){
    const uint32_t values[6]={0,c->recursion+c->waiters-1,c->recursion,c->owner,0,c->spin};
    for(unsigned i=0;i<6;++i){if(write)write32(m+i*4,values[i]);else if(read32(m+i*4)!=values[i])return 0;}
    return 1;
}
static struct fnv_windows_process *cs_process(uint32_t address,unsigned char **out){
    struct fnv_windows_process *p=thread()->process;
    if(!p->sections)sync_invalid("Windows critical-section provider unavailable",address);
    *out=sync_pointer(p,address,24);return p;
}
static void cs_initialize(int has_spin){
    uint32_t address=fnv_cpu_argument_u32(0),spin=has_spin?fnv_cpu_argument_u32(1):0;unsigned char *m;
    struct fnv_windows_process *p=cs_process(address,&m);const char *failure=NULL;uint32_t success=0;
    fnv_host_mutex_lock(&p->sync_guard);
    if(cs_find(p,address))failure="Windows critical section already initialized";
    else {struct critical_section *slot=NULL;
        for(uint32_t i=0;i<p->config.critical_section_capacity;++i){struct critical_section *c=p->sections+i;
            if(!c->address){if(!slot)slot=c;}else if(overlap(address,24,c->address,24))failure="Windows critical sections overlap";}
        if(!failure && slot){slot->address=address;slot->owner=slot->recursion=slot->waiters=0;slot->spin=spin;(void)cs_mirror(slot,m,1);success=1;}
    }
    fnv_host_mutex_unlock(&p->sync_guard);
    if(failure)sync_invalid(failure,address);
    if(!success){if(!has_spin)sync_invalid("Windows critical-section capacity/SEH pending",address);fnv_windows_set_last_error(8);}
    fnv_cpu_return_u32(success,has_spin?8:4);
}
static void api_cs_initialize(void){cs_initialize(0);}
static void api_cs_initialize_spin(void){cs_initialize(1);}
enum cs_operation {CS_ENTER,CS_TRY,CS_LEAVE,CS_DELETE,CS_SPIN};
static void cs_access(enum cs_operation op){
    uint32_t address=fnv_cpu_argument_u32(0),spin=op==CS_SPIN?fnv_cpu_argument_u32(1):0;unsigned char *m;
    struct fnv_windows_process *p=cs_process(address,&m);uint32_t id=current.config.thread_id,value=0;const char *failure=NULL;
    fnv_host_mutex_lock(&p->sync_guard);struct critical_section *c=cs_find(p,address);
    if(!c)failure="Windows critical section not initialized";
    else if(!cs_mirror(c,m,0))failure="Windows critical section copied or modified";
    else if(op==CS_SPIN){value=c->spin;c->spin=spin;}
    else if(op==CS_DELETE){
        if(c->owner || c->waiters)failure="Windows critical section deleted while active";
        else {c->address=0;c->spin=0;memset(m,0,24);}
    }else if(op==CS_LEAVE){
        if(c->owner!=id || !c->recursion)failure="Windows critical section left by non-owner";
        else if(!--c->recursion){c->owner=0;if(c->waiters){p->sync_wakes++;fnv_host_condition_wake(&c->condition);}}
    }else if(op==CS_TRY && c->owner && c->owner!=id){p->sync_try_failures++;}
    else {
        if(c->owner && c->owner!=id){
            if((uint64_t)c->waiters+c->recursion>=INT32_MAX)failure="Windows critical-section waiters overflow";
            else {c->waiters++;(void)cs_mirror(c,m,1);
                while(c->owner && !failure){p->sync_waits++;
                    int signaled=fnv_host_condition_wait(&c->condition,&p->sync_guard);
                    if(signaled<0)failure="Windows critical-section host wait failed";
                    else if(fnv_cpu_is_faulted(p->cpu))failure="Windows critical-section peer CPU fault";
                    else if(!cs_mirror(c,m,0))failure="Windows critical section modified during wait";
                }
                c->waiters--;
            }
        }
        if(!failure){if((uint64_t)c->recursion+c->waiters>=INT32_MAX)failure="Windows critical-section recursion overflow";
            else {c->owner=id;c->recursion++;value=1;}}
    }
    if(c && op!=CS_DELETE && (!failure || fnv_cpu_is_faulted(p->cpu)))(void)cs_mirror(c,m,1);
    fnv_host_mutex_unlock(&p->sync_guard);
    if(failure)sync_invalid(failure,address);
    fnv_cpu_return_u32(value,op==CS_SPIN?8:4);
}
static void api_cs_enter(void){cs_access(CS_ENTER);}
static void api_cs_try(void){cs_access(CS_TRY);}
static void api_cs_leave(void){cs_access(CS_LEAVE);}
static void api_cs_delete(void){cs_access(CS_DELETE);}
static void api_cs_spin(void){cs_access(CS_SPIN);}
enum atomic_operation {AT_EXCHANGE,AT_COMPARE,AT_INCREMENT,AT_DECREMENT,AT_ADD};
static void atomic_access(enum atomic_operation op){
    struct fnv_windows_process *p=thread()->process;uint32_t at=fnv_cpu_argument_u32(0);
    uint32_t *target=(uint32_t *)(void *)sync_pointer(p,at,4),value=0;
    if((uintptr_t)target&3)sync_invalid("Windows atomic host alignment unavailable",at);
    _Static_assert(__atomic_always_lock_free(4,0),"guest32 atomics must be lock-free");
    if(op==AT_EXCHANGE)value=__atomic_exchange_n(target,fnv_cpu_argument_u32(1),__ATOMIC_SEQ_CST);
    else if(op==AT_COMPARE){value=fnv_cpu_argument_u32(2);uint32_t replacement=fnv_cpu_argument_u32(1);
        (void)__atomic_compare_exchange_n(target,&value,replacement,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);}
    else if(op==AT_ADD)value=__atomic_fetch_add(target,fnv_cpu_argument_u32(1),__ATOMIC_SEQ_CST);
    else value=__atomic_add_fetch(target,op==AT_INCREMENT?1u:UINT32_MAX,__ATOMIC_SEQ_CST);
    fnv_cpu_return_u32(value,op==AT_COMPARE?12:op==AT_EXCHANGE || op==AT_ADD?8:4);
}
static void api_atomic_exchange(void){atomic_access(AT_EXCHANGE);}
static void api_atomic_compare(void){atomic_access(AT_COMPARE);}
static void api_atomic_increment(void){atomic_access(AT_INCREMENT);}
static void api_atomic_decrement(void){atomic_access(AT_DECREMENT);}
static void api_atomic_add(void){atomic_access(AT_ADD);}
