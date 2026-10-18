/* Native Windows performance counter. The virtual frequency is explicitly
 * 1 GHz: host monotonic nanoseconds, independent of CPU clocks and guest RDTSC.
 * QueryPerformanceCounter has an unspecified epoch; no wall clock is exposed. */
static void api_performance_counter(void){
    uint32_t address=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    unsigned char *output=sync_pointer(p,address,8);uint64_t now;
    if(!fnv_host_now_ns(&now) || now>INT64_MAX)sync_invalid("Windows performance counter unavailable",address);
    write32(output,(uint32_t)now);write32(output+4,(uint32_t)(now>>32));fnv_cpu_return_u32(1,4);
}
static void api_performance_frequency(void){
    uint32_t address=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    unsigned char *output=sync_pointer(p,address,8);write32(output,1000000000);write32(output+4,0);fnv_cpu_return_u32(1,4);
}
static void api_sleep(void){
    uint32_t ms=fnv_cpu_argument_u32(0);struct fnv_windows_process *p=thread()->process;
    if(!ms){fnv_host_thread_sleep(0);fnv_cpu_return_u32(g_eax,4);return;}
    uint64_t now,last,deadline=UINT64_MAX;if(!fnv_host_now_ns(&now))sync_invalid("Windows Sleep clock unavailable",0);last=now;
    if(ms!=UINT32_MAX){if(now>UINT64_MAX-(uint64_t)ms*1000000)sync_invalid("Windows Sleep deadline overflow",0);deadline=now+(uint64_t)ms*1000000;}
    while(ms==UINT32_MAX || now<deadline){
        uint64_t remaining=ms==UINT32_MAX?250000000:deadline-now;uint32_t slice=remaining>=250000000?250:(uint32_t)((remaining+999999)/1000000);
        fnv_host_thread_sleep(slice);
        if(fnv_cpu_is_faulted(p->cpu))sync_invalid("Windows Sleep peer CPU fault",0);
        if(!fnv_host_now_ns(&now) || now<last)sync_invalid("Windows Sleep clock failed",0);
        last=now;
    }
    fnv_cpu_return_u32(g_eax,4);
}
