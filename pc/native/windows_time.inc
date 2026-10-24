#include "host_wallclock.h"
/* QPC is process-relative in 100 ns units (10 MHz virtual frequency).
 * This reduces magnitude/overflow exposure; it is not the CPU tick frequency. */
static int performance_sample(struct fnv_windows_process *p,uint64_t *out){
    return p->config.performance_clock_ns?p->config.performance_clock_ns(p->config.performance_clock_context,out):fnv_host_now_ns(out);
}
static enum fnv_cpu_result time_prepare(struct fnv_windows_process *p){
    if(!fnv_host_mutex_init(&p->performance_guard))return FNV_CPU_NO_MEMORY;
    if(!performance_sample(p,&p->performance_epoch)){fnv_host_mutex_destroy(&p->performance_guard);return FNV_CPU_INVALID;}
    p->performance_last=p->performance_epoch;return FNV_CPU_OK;
}
static void time_dispose(struct fnv_windows_process *p){fnv_host_mutex_destroy(&p->performance_guard);}
static void api_performance_counter(void){
    uint32_t address=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    unsigned char *output=sync_pointer(p,address,8);uint64_t now=0,value=0;int valid;
    fnv_host_mutex_lock(&p->performance_guard);
    valid=performance_sample(p,&now) && now>=p->performance_last;
    if(valid){p->performance_last=now;value=(now-p->performance_epoch)/100;}
    fnv_host_mutex_unlock(&p->performance_guard);
    if(!valid)sync_invalid("Windows performance clock failed or moved backwards",address);
    write32(output,(uint32_t)value);write32(output+4,(uint32_t)(value>>32));fnv_cpu_return_u32(1,4);
}
static void api_performance_frequency(void){
    uint32_t address=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    unsigned char *output=sync_pointer(p,address,8);write32(output,10000000);write32(output+4,0);fnv_cpu_return_u32(1,4);
}
uint32_t fnv_windows_uptime_ms(void){
    (void)thread();uint64_t value;
    if(!fnv_host_uptime_ms(&value))sync_invalid("Windows uptime clock unavailable",0);
    return (uint32_t)value; /* DWORD wraps at 2^32 milliseconds. */
}
static void api_tick_count(void){fnv_cpu_return_u32(fnv_windows_uptime_ms(),0);}
static void api_system_filetime(void){
    uint32_t address=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    unsigned char *output=sync_pointer(p,address,8);uint64_t value;
    if(!fnv_host_utc_filetime(&value))sync_invalid("Windows UTC clock unavailable",address);
    write32(output,(uint32_t)value);write32(output+4,(uint32_t)(value>>32));
    fnv_cpu_return_u32(g_eax,4);
}
static void api_sleep(void){
    uint32_t ms=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    if(!ms){fnv_host_thread_sleep(0);fnv_cpu_return_u32(g_eax,4);return;}
    uint64_t now,last,deadline=UINT64_MAX;if(!fnv_host_now_ns(&now))sync_invalid("Windows Sleep clock unavailable",0);last=now;
    if(ms!=UINT32_MAX){if(now>UINT64_MAX-(uint64_t)ms*1000000)sync_invalid("Windows Sleep deadline overflow",0);deadline=now+(uint64_t)ms*1000000;}
    while(ms==UINT32_MAX || now<deadline){
        uint64_t remaining=ms==UINT32_MAX?250000000:deadline-now;uint32_t slice=remaining>=250000000?250:(uint32_t)((remaining+999999)/1000000);
        fnv_host_thread_sleep(slice);
        if(fnv_cpu_is_faulted(p->cpu))fnv_cpu_stop_for_peer();
        if(!fnv_host_now_ns(&now) || now<last)sync_invalid("Windows Sleep clock failed",0);
        last=now;
    }
    fnv_cpu_return_u32(g_eax,4);
}
