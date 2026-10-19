/* Persistent process strings and temporary fixed local blocks. Original code.
 * Guest pointer diagnostics and host scratch allocations never hold heap_guard. */
#include "command_line.h"
static int parameters_overlap(struct fnv_windows_process *p,uint32_t at,uint32_t bytes){
    return p->config.parameters_bytes && overlap(at,bytes,p->config.parameters_address,p->config.parameters_bytes);
}
static int parameters_ascii(const char *text,uint32_t maximum,uint32_t *length){
    if(!text)return 0;
    for(uint32_t n=0;n<=maximum;++n){unsigned c=(unsigned char)text[n];if(c>127)return 0;if(!c){*length=n;return 1;}}
    return 0;
}
static enum fnv_cpu_result parameters_prepare(struct fnv_windows_process *p){
    const struct fnv_windows_config *c=&p->config;uint32_t n,path;
    if(!c->parameters_bytes)return c->parameters_address || c->command_line || c->image_path?FNV_CPU_INVALID:FNV_CPU_OK;
    if(!c->parameters_address || (c->parameters_address&4095) || (c->parameters_bytes&4095) ||
       !inside(c,c->parameters_address,c->parameters_bytes) ||
       overlap(c->parameters_address,c->parameters_bytes,c->cpu.guest_base,c->image_bytes) ||
       overlap(c->parameters_address,c->parameters_bytes,c->peb_address,4096) ||
       pool_overlap(p,c->parameters_address,c->parameters_bytes) || heap_overlap(p,c->parameters_address,c->parameters_bytes) ||
       worker_overlap(p,c->parameters_address,c->parameters_bytes) ||
       !parameters_ascii(c->command_line,32767,&n) || !parameters_ascii(c->image_path,1023,&path) || !path)return FNV_CPU_INVALID;
    uint32_t wide=(n+2)&~1u;
    if((uint64_t)wide+(n+1)*2>c->parameters_bytes)return FNV_CPU_INVALID;
    char *snapshot=malloc(n+1);if(!snapshot)return FNV_CPU_NO_MEMORY;
    memcpy(snapshot,c->command_line,n+1);memcpy(p->image_path,c->image_path,path+1);
    p->command_a=c->parameters_address;p->command_w=c->parameters_address+wide;
    memcpy(memory(p,p->command_a),snapshot,n+1);
    unsigned char *output=memory(p,p->command_w);
    for(uint32_t i=0;i<=n;++i){output[i*2]=(unsigned char)snapshot[i];output[i*2+1]=0;}
    free(snapshot);
    p->config.command_line=NULL;p->config.image_path=NULL;return FNV_CPU_OK;
}
static struct fnv_windows_process *parameters_process(void){
    struct fnv_windows_process *p=thread()->process;
    if(!p->command_a)sync_invalid("Windows process parameters unavailable",0);
    return p;
}
static void api_command_a(void){fnv_cpu_return_u32(parameters_process()->command_a,0);}
static void api_command_w(void){fnv_cpu_return_u32(parameters_process()->command_w,0);}
static void module_filename(int wide){
    uint32_t handle=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),capacity=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=thread()->process;uint32_t value=0,error=0;
    if(!capacity){error=122;goto finish;}
    /* Windows W initializes its output even when the module lookup fails;
     * the A wrapper leaves the caller's buffer untouched on that failure. */
    if(wide){
        if(!at || !inside(&p->config,at,2) || (pool_overlap(p,at,2) && !fnv_vm_access(p->config.memory,at,2,1)))
            sync_invalid("Windows module filename pointer unavailable",at);
        unsigned char *first=memory(p,at);first[0]=first[1]=0;
    }
    if(handle && handle!=p->config.cpu.guest_base){error=126;goto finish;}
    if(!p->command_a)sync_invalid("Windows process parameters unavailable",0);
    uint32_t length=(uint32_t)strlen(p->image_path),copy=length<capacity?length:capacity-1,bytes=(copy+1)*(wide?2u:1u);
    if(!at || !inside(&p->config,at,bytes) || (pool_overlap(p,at,bytes) && !fnv_vm_access(p->config.memory,at,bytes,1)))
        sync_invalid("Windows module filename pointer unavailable",at);
    unsigned char *out=memory(p,at);
    for(uint32_t i=0;i<=copy;i++){
        unsigned char ch=i==copy?0:(unsigned char)p->image_path[i];
        if(wide){out[i*2]=ch;out[i*2+1]=0;}else out[i]=ch;
    }
    value=length<capacity?length:capacity;
    if(length>=capacity)error=122;
finish:
    fnv_windows_set_last_error(error);fnv_cpu_return_u32(value,12);
}
static void api_module_filename_a(void){module_filename(0);}
static void api_module_filename_w(void){module_filename(1);}
static void api_local_free(void){
    uint32_t at=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    if(!at){fnv_cpu_return_u32(0,4);return;}
    uint32_t value=at,fault=0;
    if(p->heap.blocks){
        fnv_host_mutex_lock(&p->heap_guard);uint32_t i=heap_find(&p->heap,at);
        if(!p->heap_native_fault && i!=UINT32_MAX && p->heap.blocks[i].local==1){heap_remove_locked(&p->heap,i);value=0;}
        fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);
    }
    if(value)fnv_windows_set_last_error(6);
    heap_return(value,4,fault);
}
static void api_command_argv(void){
    uint32_t input=fnv_cpu_argument_u32(0),count_at=fnv_cpu_argument_u32(1);
    struct fnv_windows_process *p=parameters_process();unsigned char *count_out=sync_pointer(p,count_at,4);
    uint16_t *source=malloc(32768*sizeof(*source));uint32_t n=0,error=8;
    if(!source){fnv_windows_set_last_error(error);fnv_cpu_return_u32(0,8);return;}
    for(;n<32768;++n){
        uint64_t at=(uint64_t)input+n*2;unsigned char *b=NULL;
        if(input && at+2<=UINT64_C(0x100000000) && inside(&p->config,(uint32_t)at,2) &&
           (!pool_overlap(p,(uint32_t)at,2) || fnv_vm_access(p->config.memory,(uint32_t)at,2,0)))b=memory(p,(uint32_t)at);
        if(!b){free(source);sync_invalid("Windows command line pointer unavailable",input);}
        source[n]=(uint16_t)(b[0]|((unsigned)b[1]<<8));if(!source[n])break;
    }
    if(n==32768){free(source);fnv_windows_set_last_error(87);fnv_cpu_return_u32(0,8);return;}
    /* Empty input returns the executable path as a single argument, including
     * spaces; it must not be sent through ordinary command-line tokenization. */
    int empty=!n;
    if(empty){while(p->image_path[n]){source[n]=(unsigned char)p->image_path[n];n++;}source[n]=0;}
    uint32_t argc=1,units=n+1;
    if(!empty)fnv_command_parse(source,n,NULL,NULL,&argc,&units);
    uint16_t *strings=malloc((n+2)*sizeof(*strings));uint32_t *offsets=malloc((n+1)*sizeof(*offsets));
    uint32_t value=0,fault=0;
    if(strings && offsets && p->heap.blocks){
        if(empty){memcpy(strings,source,(n+1)*sizeof(*strings));offsets[0]=0;}
        else fnv_command_parse(source,n,strings,offsets,&argc,&units);
        uint32_t bytes=(argc+1)*4+units*2;
        fnv_host_mutex_lock(&p->heap_guard);
        value=heap_allocate_locked(p,&p->heap,bytes,0);
        if(value){
            p->heap.blocks[heap_find(&p->heap,value)].local=1;
            unsigned char *out=memory(p,value);uint32_t data=(argc+1)*4;
            for(uint32_t i=0;i<argc;++i)write32(out+i*4,value+data+offsets[i]*2);
            write32(out+argc*4,0);
            for(uint32_t i=0;i<units;++i){out[data+i*2]=(unsigned char)strings[i];out[data+i*2+1]=(unsigned char)(strings[i]>>8);}
        }
        fault=p->heap_native_fault;fnv_host_mutex_unlock(&p->heap_guard);
    }
    free(source);free(strings);free(offsets);
    if(value)write32(count_out,argc);else fnv_windows_set_last_error(error);
    heap_return(value,8,fault);
}
