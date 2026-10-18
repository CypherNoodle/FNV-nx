/* Unnamed, process-local events. Handles carry a slot and a nonwrapping
 * generation; they are neither host HANDLEs nor guest memory pointers. */
static enum fnv_cpu_result events_prepare(struct fnv_windows_process *p){
    uint32_t count=p->config.kernel_handle_capacity;if(!count)return FNV_CPU_OK;
    if(count>65536)return FNV_CPU_INVALID;
    struct kernel_object *items=calloc(count,sizeof(*items));if(!items)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&p->kernel_guard)){free(items);return FNV_CPU_NO_MEMORY;}
    for(uint32_t i=0;i<count;++i)if(!fnv_host_condition_init(&items[i].condition)){
        while(i)fnv_host_condition_destroy(&items[--i].condition);
        fnv_host_mutex_destroy(&p->kernel_guard);free(items);return FNV_CPU_NO_MEMORY;
    }
    for(uint32_t i=0;i<count;++i)items[i].free_next=i+1<count?i+1:UINT32_MAX;
    p->handles=items;p->kernel_free=0;return FNV_CPU_OK;
}
static void events_dispose(struct fnv_windows_process *p){
    if(!p->handles)return;
    for(uint32_t i=0;i<p->config.kernel_handle_capacity;++i)fnv_host_condition_destroy(&p->handles[i].condition);
    free(p->handles);fnv_host_mutex_destroy(&p->kernel_guard);
}
static struct fnv_windows_process *event_process(void){
    struct fnv_windows_process *p=thread()->process;
    if(!p->handles)sync_invalid("Windows event/handle provider unavailable",0);
    return p;
}
static struct kernel_object *event_lookup(struct fnv_windows_process *p,uint32_t handle){
    uint32_t slot=handle&65535,generation=(handle>>16)&4095;
    if((handle&0xf0000000u)!=0xe0000000u || !generation || slot>=p->config.kernel_handle_capacity)return NULL;
    struct kernel_object *o=p->handles+slot;
    return o->live && o->generation==generation?o:NULL;
}
static void event_recycle(struct fnv_windows_process *p,struct kernel_object *o){
    if(o->generation==4095)return; /* Never wrap a stale handle into validity. */
    o->free_next=p->kernel_free;p->kernel_free=(uint32_t)(o-p->handles);
}
enum fnv_cpu_result fnv_windows_handle_stats(struct fnv_windows_process *p,struct fnv_windows_handle_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_handle_stats s={0};
    if(p->handles){fnv_host_mutex_lock(&p->kernel_guard);
        for(uint32_t i=0;i<p->config.kernel_handle_capacity;++i){s.live_handles+=p->handles[i].live!=0;s.waiting_threads+=p->handles[i].waiters;}
        s.blocking_waits=p->kernel_waits;s.timeouts=p->kernel_timeouts;s.sets=p->kernel_sets;s.resets=p->kernel_resets;
        fnv_host_mutex_unlock(&p->kernel_guard);}
    *out=s;return FNV_CPU_OK;
}
static void api_event_create(void){
    uint32_t attributes=fnv_cpu_argument_u32(0),manual=fnv_cpu_argument_u32(1),initial=fnv_cpu_argument_u32(2),name=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=event_process();
    if(attributes || name)sync_invalid("Windows named/security event support pending",name?name:attributes);
    uint32_t handle=0;fnv_host_mutex_lock(&p->kernel_guard);
    if(p->kernel_free!=UINT32_MAX){uint32_t i=p->kernel_free;struct kernel_object *o=p->handles+i;p->kernel_free=o->free_next;
        o->generation++;o->live=1;o->manual=manual!=0;o->signaled=initial!=0;o->waiting=NULL;
        handle=0xe0000000u|(o->generation<<16)|i;
    }
    fnv_host_mutex_unlock(&p->kernel_guard);fnv_windows_set_last_error(handle?0:8);fnv_cpu_return_u32(handle,16);
}
static void event_signal(int reset){
    uint32_t handle=fnv_cpu_argument_u32(0),value=0;struct fnv_windows_process *p=event_process();
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o){value=1;if(reset){o->signaled=0;p->kernel_resets++;}
        else {p->kernel_sets++;uint32_t granted=0;
            while(o->waiting){struct event_waiter *w=o->waiting;o->waiting=w->next;w->released=1;granted=1;if(!o->manual)break;}
            if(o->manual || !granted)o->signaled=1;
            if(granted)fnv_host_condition_wake_all(&o->condition);
        }
    }
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(6);fnv_cpu_return_u32(value,4);
}
static void api_event_set(void){event_signal(0);}
static void api_event_reset(void){event_signal(1);}
static void api_close_handle(void){
    uint32_t handle=fnv_cpu_argument_u32(0),value=0;struct fnv_windows_process *p=event_process();
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o){o->live=0;o->signaled=0;value=1;if(o->waiters)fnv_host_condition_wake_all(&o->condition);else event_recycle(p,o);}
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(6);fnv_cpu_return_u32(value,4);
}
static void api_wait_single(void){
    uint32_t handle=fnv_cpu_argument_u32(0),ms=fnv_cpu_argument_u32(1),value=UINT32_MAX,error=6;
    struct fnv_windows_process *p=event_process();const char *failure=NULL;
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o){
        if(o->signaled){if(!o->manual)o->signaled=0;value=0;}
        else if(!ms){value=0x102;p->kernel_timeouts++;}
        else {
            uint64_t now=0,last=0,deadline=UINT64_MAX;
            if(!fnv_host_now_ns(&now))failure="Windows event monotonic clock unavailable";
            else if(ms!=UINT32_MAX && now>UINT64_MAX-(uint64_t)ms*1000000)failure="Windows event deadline overflow";
            else {last=now;if(ms!=UINT32_MAX)deadline=now+(uint64_t)ms*1000000;}
            struct event_waiter waiter={o->waiting,0};o->waiting=&waiter;o->waiters++;
            while(!failure && o->live && !waiter.released){
                if(!fnv_host_now_ns(&now) || now<last){failure="Windows event monotonic clock failed";break;}last=now;
                if(ms!=UINT32_MAX && now>=deadline){value=0x102;p->kernel_timeouts++;break;}
                uint64_t remaining=ms==UINT32_MAX?250000000:deadline-now;uint32_t slice=remaining>=250000000?250:(uint32_t)((remaining+999999)/1000000);
                p->kernel_waits++;int status=fnv_host_condition_wait_ms(&o->condition,&p->kernel_guard,slice);
                if(status<0)failure="Windows event host wait failed";
                else if(fnv_cpu_is_faulted(p->cpu))failure="Windows event peer CPU fault";
            }
            if(!failure){if(!o->live){value=UINT32_MAX;error=6;}else if(waiter.released)value=0;}
            if(!waiter.released){struct event_waiter **link=&o->waiting;while(*link!=&waiter)link=&(*link)->next;*link=waiter.next;}
            o->waiters--;if(!o->live && !o->waiters)event_recycle(p,o);
        }
    }
    fnv_host_mutex_unlock(&p->kernel_guard);
    if(failure)sync_invalid(failure,handle);
    if(value==UINT32_MAX)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
