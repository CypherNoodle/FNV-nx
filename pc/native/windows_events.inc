/* Unnamed, process-local synchronization. Handles carry a slot and a nonwrapping
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
/* All object transitions and wait grants use kernel_guard. A wait-all only
 * consumes state after every object qualifies. Granted requests are removed
 * before waking, so ResetEvent cannot revoke a completed grant. */
static int kernel_ready(struct kernel_object *o,uint32_t owner){
    return o->kind==KERNEL_MUTEX?(!o->owner || o->owner==owner):o->signaled!=0;
}
static int kernel_acquire(struct kernel_wait *w){
    uint32_t chosen=UINT32_MAX;
    for(uint32_t i=0;i<w->count;++i){
        if(!w->objects[i]->live){w->result=UINT32_MAX;return 1;}
    }
    for(uint32_t i=0;i<w->count;++i){
        int ready=kernel_ready(w->objects[i],w->owner);
        if(w->all && !ready)return 0;
        if(ready && chosen==UINT32_MAX)chosen=i;
    }
    if(chosen==UINT32_MAX)return 0;
    uint32_t abandoned=UINT32_MAX;
    for(uint32_t i=0;i<w->count;++i){
        if(!w->all && i!=chosen)continue;
        struct kernel_object *o=w->objects[i];
        if(o->kind==KERNEL_MUTEX){
            if(o->recursion==UINT32_MAX){w->result=UINT32_MAX;return 1;}
        }
    }
    for(uint32_t i=0;i<w->count;++i){
        if(!w->all && i!=chosen)continue;
        struct kernel_object *o=w->objects[i];
        if(o->kind==KERNEL_MUTEX){
            if(o->abandoned && abandoned==UINT32_MAX)abandoned=i;
            o->owner=w->owner;o->recursion++;o->abandoned=0;
        }else if(o->kind==KERNEL_SEMAPHORE)o->signaled--;
        else if(!o->manual)o->signaled=0;
    }
    w->result=abandoned!=UINT32_MAX?0x80+(w->all?0:abandoned):(w->all?0:chosen);
    return 1;
}
static void kernel_dispatch(struct fnv_windows_process *p){
    struct kernel_wait **link=&p->kernel_pending;
    while(*link){struct kernel_wait *w=*link;
        if(kernel_acquire(w)){*link=w->next;w->released=1;fnv_host_condition_wake_all(&w->objects[0]->condition);}
        else link=&w->next;
    }
}
static void kernel_thread_exit(struct fnv_windows_process *p,uint32_t owner){
    if(!p->handles)return;
    fnv_host_mutex_lock(&p->kernel_guard);
    for(uint32_t i=0;i<p->config.kernel_handle_capacity;++i){struct kernel_object *o=p->handles+i;
        if(o->live && o->kind==KERNEL_MUTEX && o->owner==owner){o->owner=0;o->recursion=0;o->abandoned=1;}
    }
    kernel_dispatch(p);fnv_host_mutex_unlock(&p->kernel_guard);
}
enum fnv_cpu_result fnv_windows_handle_stats(struct fnv_windows_process *p,struct fnv_windows_handle_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_handle_stats s={0};
    if(p->handles){fnv_host_mutex_lock(&p->kernel_guard);
        for(uint32_t i=0;i<p->config.kernel_handle_capacity;++i)s.live_handles+=p->handles[i].live!=0;
        s.waiting_threads=p->kernel_waiting;
        s.blocking_waits=p->kernel_waits;s.timeouts=p->kernel_timeouts;s.sets=p->kernel_sets;s.resets=p->kernel_resets;
        fnv_host_mutex_unlock(&p->kernel_guard);}
    *out=s;return FNV_CPU_OK;
}
static uint32_t kernel_create(struct fnv_windows_process *p,uint32_t kind,uint32_t manual,uint32_t initial,uint32_t maximum){
    uint32_t handle=0;fnv_host_mutex_lock(&p->kernel_guard);
    if(p->kernel_free!=UINT32_MAX){uint32_t i=p->kernel_free;struct kernel_object *o=p->handles+i;p->kernel_free=o->free_next;
        o->generation++;o->live=1;o->kind=kind;o->manual=manual!=0;o->signaled=initial;o->maximum=maximum;
        o->owner=kind==KERNEL_MUTEX && initial?current.config.thread_id:0;o->recursion=o->owner?1:0;o->abandoned=0;
        handle=0xe0000000u|(o->generation<<16)|i;
    }
    fnv_host_mutex_unlock(&p->kernel_guard);fnv_windows_set_last_error(handle?0:8);return handle;
}
static void kernel_security(uint32_t attributes,uint32_t name){
    if(attributes || name)sync_invalid("Windows named/security synchronization support pending",name?name:attributes);
}
static void api_event_create(void){
    uint32_t attributes=fnv_cpu_argument_u32(0),manual=fnv_cpu_argument_u32(1),initial=fnv_cpu_argument_u32(2),name=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=event_process();kernel_security(attributes,name);
    fnv_cpu_return_u32(kernel_create(p,KERNEL_EVENT,manual,initial!=0,1),16);
}
static void api_semaphore_create(void){
    uint32_t attributes=fnv_cpu_argument_u32(0),initial=fnv_cpu_argument_u32(1),maximum=fnv_cpu_argument_u32(2),name=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=event_process();kernel_security(attributes,name);
    if(!maximum || maximum>INT32_MAX || initial>maximum){fnv_windows_set_last_error(87);fnv_cpu_return_u32(0,16);return;}
    fnv_cpu_return_u32(kernel_create(p,KERNEL_SEMAPHORE,0,initial,maximum),16);
}
static void api_mutex_create(void){
    uint32_t attributes=fnv_cpu_argument_u32(0),initial=fnv_cpu_argument_u32(1),name=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=event_process();kernel_security(attributes,name);
    fnv_cpu_return_u32(kernel_create(p,KERNEL_MUTEX,0,initial!=0,0),12);
}
static void event_signal(int reset){
    uint32_t handle=fnv_cpu_argument_u32(0),value=0;struct fnv_windows_process *p=event_process();
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o && o->kind==KERNEL_EVENT){value=1;if(reset){o->signaled=0;p->kernel_resets++;}
        else {p->kernel_sets++;o->signaled=1;kernel_dispatch(p);}
    }
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(6);fnv_cpu_return_u32(value,4);
}
static void api_event_set(void){event_signal(0);}
static void api_event_reset(void){event_signal(1);}
static void api_semaphore_release(void){
    uint32_t handle=fnv_cpu_argument_u32(0),count=fnv_cpu_argument_u32(1),previous=fnv_cpu_argument_u32(2),value=0,error=6;
    struct fnv_windows_process *p=event_process();unsigned char *output=previous?sync_pointer(p,previous,4):NULL;
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o && o->kind==KERNEL_SEMAPHORE){
        if(!count || count>INT32_MAX)error=87;
        else if(count>o->maximum-o->signaled)error=298;
        else {if(output)write32(output,o->signaled);o->signaled+=count;value=1;kernel_dispatch(p);}
    }
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(error);fnv_cpu_return_u32(value,12);
}
static void api_mutex_release(void){
    uint32_t handle=fnv_cpu_argument_u32(0),value=0,error=6;struct fnv_windows_process *p=event_process();
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o && o->kind==KERNEL_MUTEX){if(o->owner!=current.config.thread_id)error=288;
        else {value=1;if(!--o->recursion){o->owner=0;kernel_dispatch(p);}}
    }
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(error);fnv_cpu_return_u32(value,4);
}
static void api_close_handle(void){
    uint32_t handle=fnv_cpu_argument_u32(0),value=0;struct fnv_windows_process *p=event_process();
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
    if(o){o->live=0;o->signaled=0;value=1;kernel_dispatch(p);if(!o->waiters)event_recycle(p,o);}
    fnv_host_mutex_unlock(&p->kernel_guard);if(!value)fnv_windows_set_last_error(6);fnv_cpu_return_u32(value,4);
}
static uint32_t kernel_wait(struct fnv_windows_process *p,const uint32_t *handles,uint32_t count,uint32_t all,uint32_t ms){
    struct kernel_wait waiter={0};waiter.count=count;waiter.all=all;waiter.owner=current.config.thread_id;
    uint32_t value=UINT32_MAX;const char *failure=NULL;
    fnv_host_mutex_lock(&p->kernel_guard);
    for(uint32_t i=0;i<count;++i){waiter.objects[i]=event_lookup(p,handles[i]);if(!waiter.objects[i])goto done;}
    if(kernel_acquire(&waiter)){value=waiter.result;goto done;}
    if(!ms){value=0x102;p->kernel_timeouts++;goto done;}
    uint64_t now=0,last=0,deadline=UINT64_MAX;
    if(!fnv_host_now_ns(&now))failure="Windows event monotonic clock unavailable";
    else if(ms!=UINT32_MAX && now>UINT64_MAX-(uint64_t)ms*1000000)failure="Windows event deadline overflow";
    else {last=now;if(ms!=UINT32_MAX)deadline=now+(uint64_t)ms*1000000;}
    waiter.next=p->kernel_pending;p->kernel_pending=&waiter;
    p->kernel_waiting++;
    for(uint32_t i=0;i<count;++i)waiter.objects[i]->waiters++;
    while(!failure && !waiter.released){
        if(!fnv_host_now_ns(&now) || now<last){failure="Windows event monotonic clock failed";break;}last=now;
        if(ms!=UINT32_MAX && now>=deadline){value=0x102;p->kernel_timeouts++;break;}
        uint64_t remaining=ms==UINT32_MAX?250000000:deadline-now;uint32_t slice=remaining>=250000000?250:(uint32_t)((remaining+999999)/1000000);
        p->kernel_waits++;int status=fnv_host_condition_wait_ms(&waiter.objects[0]->condition,&p->kernel_guard,slice);
        if(status<0)failure="Windows event host wait failed";
        else if(fnv_cpu_is_faulted(p->cpu))failure="Windows event peer CPU fault";
    }
    if(waiter.released)value=waiter.result;
    else {struct kernel_wait **link=&p->kernel_pending;while(*link!=&waiter)link=&(*link)->next;*link=waiter.next;}
    p->kernel_waiting--;
    for(uint32_t i=0;i<count;++i){struct kernel_object *o=waiter.objects[i];
        if(!o->live)value=UINT32_MAX;
        o->waiters--;if(!o->live && !o->waiters)event_recycle(p,o);
    }
done:
    fnv_host_mutex_unlock(&p->kernel_guard);
    if(failure)sync_invalid(failure,handles[0]);
    if(value==UINT32_MAX)fnv_windows_set_last_error(6);
    return value;
}
static void api_wait_single(void){
    uint32_t handle=fnv_cpu_argument_u32(0),ms=fnv_cpu_argument_u32(1);struct fnv_windows_process *p=event_process();
    fnv_cpu_return_u32(kernel_wait(p,&handle,1,0,ms),8);
}
static void api_wait_multiple(void){
    uint32_t count=fnv_cpu_argument_u32(0),address=fnv_cpu_argument_u32(1),all=fnv_cpu_argument_u32(2),ms=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=event_process();
    if(!count || count>64){fnv_windows_set_last_error(87);fnv_cpu_return_u32(UINT32_MAX,16);return;}
    if(!address || (address&3) || !inside(&p->config,address,count*4))sync_invalid("Windows wait array unavailable",address);
    if(pool_overlap(p,address,count*4) && !fnv_vm_access(p->config.memory,address,count*4,0))sync_invalid("Windows wait array not readable",address);
    unsigned char *input=memory(p,address);uint32_t handles[64];
    for(uint32_t i=0;i<count;++i){handles[i]=read32(input+i*4);
        for(uint32_t j=0;j<i;++j)if(handles[j]==handles[i]){fnv_windows_set_last_error(87);fnv_cpu_return_u32(UINT32_MAX,16);return;}
    }
    fnv_cpu_return_u32(kernel_wait(p,handles,count,all!=0,ms),16);
}
