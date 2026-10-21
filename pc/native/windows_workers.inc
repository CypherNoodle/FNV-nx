/* Original process-local managed threads. Guest and native stacks are distinct.
 * Caller provides a dedicated committed guest arena; callbackless EXE TLS templates supported. */
static int worker_overlap(struct fnv_windows_process *p,uint32_t at,uint32_t bytes){
    return p->config.worker_bytes && overlap(at,bytes,p->config.worker_address,p->config.worker_bytes);
}
static enum fnv_cpu_result workers_prepare(struct fnv_windows_process *p){
    const struct fnv_windows_config *c=&p->config;
    if(!c->worker_capacity)return c->worker_address || c->worker_bytes || c->worker_stack_bytes || c->worker_host_stack_bytes || c->worker_tls_bytes?FNV_CPU_INVALID:FNV_CPU_OK;
    uint64_t stride=(uint64_t)c->worker_stack_bytes+8192+c->worker_tls_bytes;
    if((c->worker_tls_bytes&4095) || c->worker_capacity>256 || !p->handles || !c->worker_address || (c->worker_address&4095) ||
       c->worker_stack_bytes<16384 || (c->worker_stack_bytes&4095) ||
       c->worker_host_stack_bytes<65536 || (c->worker_host_stack_bytes&4095) ||
       stride*c->worker_capacity>c->worker_bytes || stride*c->worker_capacity>UINT32_MAX-c->worker_address ||
       !inside(c,c->worker_address,c->worker_bytes) ||
       overlap(c->worker_address,c->worker_bytes,c->cpu.guest_base,c->image_bytes) ||
       overlap(c->worker_address,c->worker_bytes,c->peb_address,4096) ||
       pool_overlap(p,c->worker_address,c->worker_bytes) || heap_overlap(p,c->worker_address,c->worker_bytes))return FNV_CPU_INVALID;
    struct managed_worker *items=calloc(c->worker_capacity,sizeof(*items));if(!items)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&p->worker_guard)){free(items);return FNV_CPU_NO_MEMORY;}
    for(uint32_t i=0;i<c->worker_capacity;++i){
        if(!fnv_host_condition_init(&items[i].condition)){
            while(i)fnv_host_condition_destroy(&items[--i].condition);
            fnv_host_mutex_destroy(&p->worker_guard);free(items);return FNV_CPU_NO_MEMORY;
        }
        items[i].process=p;
    }
    p->workers=items;p->worker_next_id=0x80000000u;return FNV_CPU_OK;
}
/* Hold worker_guard. FINISHED is published after detach and the last use of
 * guest resources. The native wrapper then only returns, so joining is safe. */
static int workers_reap(struct fnv_windows_process *p){
    for(uint32_t i=0;i<p->config.worker_capacity;++i){struct managed_worker *w=p->workers+i;
        if(w->phase==3){if(!fnv_host_thread_join(&w->host))return 0;w->phase=0;w->object=NULL;p->worker_reaped++;}
    }
    return 1;
}
static int workers_idle(struct fnv_windows_process *p){
    if(!p->workers)return 1;
    fnv_host_mutex_lock(&p->worker_guard);int idle=workers_reap(p);
    for(uint32_t i=0;i<p->config.worker_capacity;++i)if(p->workers[i].phase)idle=0;
    fnv_host_mutex_unlock(&p->worker_guard);return idle;
}
static void workers_dispose(struct fnv_windows_process *p){
    if(!p->workers)return;
    for(uint32_t i=0;i<p->config.worker_capacity;++i)fnv_host_condition_destroy(&p->workers[i].condition);
    free(p->workers);fnv_host_mutex_destroy(&p->worker_guard);
}
enum fnv_cpu_result fnv_windows_worker_stats(struct fnv_windows_process *p,struct fnv_windows_worker_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_worker_stats s={0};s.capacity=p->config.worker_capacity;
    if(p->workers){fnv_host_mutex_lock(&p->worker_guard);
        for(uint32_t i=0;i<s.capacity;++i){uint32_t phase=p->workers[i].phase;s.active+=phase==1 || phase==2;s.finished+=phase==3;}
        s.created=p->worker_created;s.reaped=p->worker_reaped;s.failures=p->worker_failures;fnv_host_mutex_unlock(&p->worker_guard);
    }
    *out=s;return FNV_CPU_OK;
}
static void worker_poison(struct managed_worker *w,const char *text){
    struct fnv_cpu_fault fault={0};fault.kind=FNV_CPU_UNIMPLEMENTED;fault.entry=w->entry;
    snprintf(fault.text,sizeof(fault.text),"%s",text);fnv_cpu_invalidate(w->process->cpu,&fault);
}
static void worker_entry(void *context){
    struct managed_worker *w=context;struct fnv_windows_process *p=w->process;managed_current=w;
    enum fnv_cpu_result result=fnv_windows_thread_attach(p,&w->config);uint32_t code=0xc0000001u;
    if(result==FNV_CPU_OK){
        fnv_host_mutex_lock(&p->worker_guard);
        while(w->suspended && !fnv_cpu_is_faulted(p->cpu)){
            if(fnv_host_condition_wait_ms(&w->condition,&p->worker_guard,250)<0){
                fnv_host_mutex_unlock(&p->worker_guard);worker_poison(w,"Windows suspended worker host wait failed");fnv_host_mutex_lock(&p->worker_guard);break;
            }
        }
        w->phase=2;fnv_host_mutex_unlock(&p->worker_guard);
        if(!fnv_cpu_is_faulted(p->cpu)){
            uint32_t stack=w->config.state.gpr[4];write32(memory(p,stack),0);write32(memory(p,stack+4),w->parameter);
            struct fnv_cpu_fault fault;result=fnv_cpu_execute(w->entry,&fault);
            if(result==FNV_CPU_OK || result==FNV_CPU_THREAD_EXIT){struct fnv_cpu_state state={0};
                if(fnv_cpu_get_state(&state)==FNV_CPU_OK)code=state.gpr[0];
                if(result==FNV_CPU_OK && state.gpr[4]!=stack+8)worker_poison(w,"Windows ThreadProc violated stdcall stack ABI");
            }
        }
        if(fnv_windows_thread_detach()!=FNV_CPU_OK){worker_poison(w,"Windows worker exited with owned critical section");
            if(fnv_windows_thread_detach()!=FNV_CPU_OK)abort();
        }
    }else if(!fnv_cpu_is_faulted(p->cpu))worker_poison(w,"Windows managed worker attach failed");
    /* Detach (including mutex abandonment) precedes the signaled thread handle. */
    fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=w->object;o->state->maximum=code;o->state->signaled=1;
    kernel_dispatch(p);o->waiters--;if(!o->live && !o->waiters)event_recycle(p,o);fnv_host_mutex_unlock(&p->kernel_guard);
    fnv_host_mutex_lock(&p->worker_guard);w->phase=3;if(fnv_cpu_is_faulted(p->cpu))p->worker_failures++;
    fnv_host_mutex_unlock(&p->worker_guard);managed_current=NULL;
}
static void api_thread_create(void){
    uint32_t attributes=fnv_cpu_argument_u32(0),stack_bytes=fnv_cpu_argument_u32(1),entry=fnv_cpu_argument_u32(2),parameter=fnv_cpu_argument_u32(3);
    uint32_t flags=fnv_cpu_argument_u32(4),id_address=fnv_cpu_argument_u32(5);struct fnv_windows_process *p=thread()->process;
    uint32_t saved_error=read32(current.teb+TEB_ERROR);
    if(!p->workers)sync_invalid("Windows managed thread provider unavailable",entry);
    struct fnv_pe_tls_info tls;fnv_pe_tls_describe(p->tls,&tls);
    if(p->tls && p->config.worker_tls_bytes<(uint64_t)tls.initialized_bytes+tls.zero_bytes+tls.alignment+4)
        sync_invalid("Windows managed worker PE static TLS budget unavailable",p->image_tls_rva);
    if(attributes || (flags&~4u))sync_invalid("Windows thread security/creation flags pending",attributes?attributes:flags);
    unsigned char *id_output=id_address?sync_pointer(p,id_address,4):NULL;
    if(stack_bytes>p->config.worker_stack_bytes){fnv_windows_set_last_error(8);fnv_cpu_return_u32(0,24);return;}
    uint32_t handle=0,error=8;fnv_host_mutex_lock(&p->worker_guard);
    if(!workers_reap(p)){fnv_host_mutex_unlock(&p->worker_guard);sync_invalid("Windows worker join failed",entry);}
    struct managed_worker *w=NULL;
    for(uint32_t i=0;i<p->config.worker_capacity;++i)if(!p->workers[i].phase){w=p->workers+i;break;}
    if(w && p->worker_next_id!=UINT32_MAX){
        handle=kernel_create(p,KERNEL_THREAD,1,0,259);
        if(handle){uint32_t i=(uint32_t)(w-p->workers),base=p->config.worker_address+i*(p->config.worker_stack_bytes+8192+p->config.worker_tls_bytes);
            memset(&w->config,0,sizeof(w->config));w->config.thread_id=++p->worker_next_id;w->config.teb_address=base;w->config.tls_expansion_address=base+4096;
            if(p->tls){w->config.static_tls_vector=base+8192;w->config.static_tls_vector_bytes=4;
                w->config.static_tls_data=(w->config.static_tls_vector+4+tls.alignment-1)&~(tls.alignment-1);
                w->config.static_tls_data_bytes=p->config.worker_tls_bytes-(w->config.static_tls_data-w->config.static_tls_vector);}
            w->config.stack_low=base+8192+p->config.worker_tls_bytes;w->config.stack_high=w->config.stack_low+p->config.worker_stack_bytes;
            fnv_cpu_state_init(&w->config.state,w->config.stack_high-16,w->config.teb_address);
            w->entry=entry;w->parameter=parameter;w->suspended=(flags&4)!=0;w->phase=1;
            fnv_host_mutex_lock(&p->kernel_guard);w->object=event_lookup(p,handle);w->object->waiters++;w->object->state->owner=w->config.thread_id;fnv_host_mutex_unlock(&p->kernel_guard);
            if(!fnv_host_thread_create(&w->host,worker_entry,w,p->config.worker_host_stack_bytes)){
                fnv_host_mutex_lock(&p->kernel_guard);w->object->waiters--;w->object->live=0;event_recycle(p,w->object);fnv_host_mutex_unlock(&p->kernel_guard);
                w->object=NULL;w->phase=0;handle=0;p->worker_failures++;
            }else {p->worker_created++;if(id_output)write32(id_output,w->config.thread_id);}
        }
    }
    fnv_host_mutex_unlock(&p->worker_guard);fnv_windows_set_last_error(handle?saved_error:error);fnv_cpu_return_u32(handle,24);
}
static void api_thread_exit(void){
    uint32_t code=fnv_cpu_argument_u32(0);thread();
    if(!managed_current)sync_invalid("Windows ExitThread requires managed worker",code);
    fnv_cpu_exit_thread(code);
}
static void api_current_thread(void){thread();fnv_cpu_return_u32(0xfffffffeu,0);}
static void api_thread_query(int id){
    uint32_t handle=fnv_cpu_argument_u32(0),output=id?0:fnv_cpu_argument_u32(1),value=0,answer=0;
    struct fnv_windows_process *p=thread()->process;unsigned char *destination=id?NULL:sync_pointer(p,output,4);
    if(handle==0xfffffffeu){value=1;answer=id?current.config.thread_id:259;}
    else if(p->handles){fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
        if(o && o->state->kind==KERNEL_THREAD){value=1;answer=id?o->state->owner:o->state->maximum;}fnv_host_mutex_unlock(&p->kernel_guard);
    }
    if(!value)fnv_windows_set_last_error(6);else if(destination)write32(destination,answer);
    fnv_cpu_return_u32(id?answer:value,id?4:8);
}
static void api_thread_exit_code(void){api_thread_query(0);}
static void api_thread_handle_id(void){api_thread_query(1);}
static void api_thread_resume(void){
    uint32_t handle=fnv_cpu_argument_u32(0),value=UINT32_MAX;struct fnv_windows_process *p=thread()->process;
    if(p->workers){fnv_host_mutex_lock(&p->worker_guard);fnv_host_mutex_lock(&p->kernel_guard);struct kernel_object *o=event_lookup(p,handle);
        if(o && o->state->kind==KERNEL_THREAD){value=0;
            for(uint32_t i=0;i<p->config.worker_capacity;++i){struct managed_worker *w=p->workers+i;
                if(w->phase && w->phase!=3 && w->object==o){value=w->suspended;w->suspended=0;fnv_host_condition_wake_all(&w->condition);break;}
            }
        }
        fnv_host_mutex_unlock(&p->kernel_guard);fnv_host_mutex_unlock(&p->worker_guard);
    }
    if(value==UINT32_MAX)fnv_windows_set_last_error(6);
    fnv_cpu_return_u32(value,4);
}
