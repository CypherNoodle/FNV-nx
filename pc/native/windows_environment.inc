/* Original virtual environment: immutable host snapshot, independent guest
 * copies, no inherited host paths. ASCII is identical in OEM and UTF-16. */
static enum fnv_cpu_result environment_prepare(struct fnv_windows_process *p){
    const char *source=p->config.environment;uint32_t bytes=p->config.environment_bytes;
    if(!source && !bytes){source="\0";bytes=2;}
    else {
        if(!source || bytes<2 || bytes>65536 || source[bytes-1] || source[bytes-2])return FNV_CPU_INVALID;
        uint32_t at=0;
        if(!source[0]){if(bytes!=2)return FNV_CPU_INVALID;}
        else while(at<bytes-1){
            uint32_t begin=at,equal=0;
            while(at<bytes-1 && source[at]){
                unsigned c=(unsigned char)source[at];if(c>127)return FNV_CPU_INVALID;
                if(c=='=' && !equal)equal=at-begin+1;
                at++;
            }
            if(equal<2 || at==begin || at>=bytes-1)return FNV_CPU_INVALID;
            at++;
            if(!source[at] && at!=bytes-1)return FNV_CPU_INVALID;
        }
    }
    char *copy=malloc(bytes);if(!copy)return FNV_CPU_NO_MEMORY;
    memcpy(copy,source,bytes);p->environment=copy;p->environment_bytes=bytes;
    p->config.environment=NULL;p->config.environment_bytes=0;return FNV_CPU_OK;
}
static void environment_get(int wide){
    struct fnv_windows_process *p=thread()->process;uint32_t value=0,fault=0;
    if(p->heap.blocks){
        fnv_host_mutex_lock(&p->heap_guard);
        value=heap_allocate_locked(p,&p->heap,p->environment_bytes*(wide?2u:1u),0);
        if(value){
            p->heap.blocks[heap_find(&p->heap,value)].local=wide?3:2;
            unsigned char *out=memory(p,value);
            for(uint32_t i=0;i<p->environment_bytes;i++){
                if(wide){out[i*2]=(unsigned char)p->environment[i];out[i*2+1]=0;}
                else out[i]=(unsigned char)p->environment[i];
            }
        }
        fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);
    }
    if(!value)fnv_windows_set_last_error(8);
    heap_return(value,0,fault);
}
static void environment_free(int wide){
    struct fnv_windows_process *p=thread()->process;uint32_t at=fnv_cpu_argument_u32(0),value=0,fault=0;
    if(p->heap.blocks){
        fnv_host_mutex_lock(&p->heap_guard);uint32_t i=heap_find(&p->heap,at);
        if(!p->heap_native_fault && i!=UINT32_MAX && p->heap.blocks[i].local==(uint32_t)(wide?3:2)){
            heap_remove_locked(&p->heap,i);value=1;
        }
        fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);
    }
    /* Defensive policy for pointers outside the documented matching-API
     * contract. Do not inspect arbitrary guest memory or free another owner. */
    if(!value)fnv_windows_set_last_error(87);
    heap_return(value,4,fault);
}
static void api_environment_a(void){environment_get(0);}
static void api_environment_w(void){environment_get(1);}
static void api_environment_free_a(void){environment_free(0);}
static void api_environment_free_w(void){environment_free(1);}
