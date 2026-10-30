#include <switch.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "host_thread.h"
#include "engine_watchdog.h"
#include "engine_pause_sample.h"
#include "cpu_runtime.h"
#include "engine_profile.h"
#include "d3d9_vk_progress.h"
extern void _start(void);
extern int fnv_engine_keyboard_is_open(void);
/* No guest TLS, callbacks, provider stats, NWindow, audout or rumble calls. */
static atomic_uint monitor_stage;
static atomic_uint_fast64_t monitor_tick;
static fnv_host_thread observer;
static FILE *output;
static Handle monitor_handle,guest_handle;
static struct fnv_vk_backend *renderer;
struct captured {Handle target;ThreadContext registers;u64 frames[24][2];unsigned count;};
static int readable(u64 at){
    MemoryInfo info;u32 page;
    return at && at<=UINT64_MAX-16 && R_SUCCEEDED(svcQueryMemory(&info,&page,at)) &&
        (info.perm&Perm_R) && info.addr<=at && info.size>=16 && at-info.addr<=info.size-16;
}
static uint32_t pause_target(void *ctx){return svcSetThreadActivity(((struct captured*)ctx)->target,ThreadActivity_Paused);}
static uint32_t resume_target(void *ctx){return svcSetThreadActivity(((struct captured*)ctx)->target,ThreadActivity_Runnable);}
static uint32_t capture_target(void *ctx){
    struct captured *s=ctx;Result rc=svcGetThreadContext3(&s->registers,s->target);if(R_FAILED(rc))return rc;
    u64 fp=s->registers.fp;
    for(unsigned i=0;i<24 && !(fp&15) && readable(fp);i++){
        const volatile u64 *p=(const volatile u64*)(uintptr_t)fp;u64 next=p[0],lr=p[1];
        s->frames[s->count][0]=fp;s->frames[s->count++][1]=lr;
        if(next<=fp || next-fp>(8u<<20))break;
        fp=next;
    }
    return 0;
}
static void sample(const char *name,Handle handle){
    if(!handle || handle==CUR_THREAD_HANDLE || handle==CUR_PROCESS_HANDLE || handle==threadGetCurHandle()){
        fprintf(output,"Context %s unavailable: invalid/self target handle\n",name);return;
    }
    if(!envIsSyscallHinted(0x32) || !envIsSyscallHinted(0x33)){
        fprintf(output,"Context %s unavailable: pause/context SVC not hinted\n",name);return;
    }
    struct captured s={.target=handle};struct fnv_pause_ops ops={&s,pause_target,capture_target,resume_target};
    struct fnv_pause_result r=fnv_pause_sample(&ops);
    /* Always restore before stdio/allocation: the target may hold libc locks. */
    if(!r.pause && R_FAILED(r.resume)){
        Result retried=resume_target(&s);
        if(R_FAILED(retried))svcExitProcess();
    }
    fprintf(output,"Context %s pause=%08lX capture=%08lX resume=%08lX\n",name,(unsigned long)r.pause,(unsigned long)r.capture,(unsigned long)r.resume);
    if(r.pause || r.capture)return;
    u64 base=(u64)(uintptr_t)_start;
    fprintf(output,"pc=%016llX pc_offset=%016llX lr=%016llX lr_offset=%016llX sp=%016llX fp=%016llX\n",(unsigned long long)s.registers.pc.x,(unsigned long long)(s.registers.pc.x-base),(unsigned long long)s.registers.lr,(unsigned long long)(s.registers.lr-base),(unsigned long long)s.registers.sp,(unsigned long long)s.registers.fp);
    for(unsigned i=19;i<29;i++)fprintf(output,"x%u=%016llX%c",i,(unsigned long long)s.registers.cpu_gprs[i].x,i==28?'\n':' ');
    for(unsigned i=0;i<s.count;i++)fprintf(output,"frame=%u fp=%016llX return=%016llX return_offset=%016llX\n",i,(unsigned long long)s.frames[i][0],(unsigned long long)s.frames[i][1],(unsigned long long)(s.frames[i][1]-base));
}
static void watch(void *unused){
    (void)unused;PadState pad;padInitializeDefault(&pad);u64 start=armGetSystemTick(),last=start,held=0,last_submit=start,submits=0;unsigned samples=0;int was_stalled=0;
    const char *names[]={"start","applet","pad","keyboard","mouse","dinput","user32","sound","controller","display","stop","stats","sleep"};
    fprintf(output,"Watchdog image_base=%016llX independent_io=1 capture_only_after_stall=1\n",(unsigned long long)(uintptr_t)_start);fflush(output);
    for(;;){
        u64 now=armGetSystemTick();padUpdate(&pad);u64 buttons=padGetButtons(&pad),combo=HidNpadButton_L|HidNpadButton_R|HidNpadButton_Minus;
        if((buttons&combo)==combo){if(!held)held=now;else if(armTicksToNs(now-held)>=1000000000ull){
            fprintf(output,"Independent diagnostic exit requested\n");sample("monitor",monitor_handle);sample("guest-main",guest_handle);fflush(output);svcExitProcess();
        }}else held=0;
        if(armTicksToNs(now-last)>=1000000000ull){
            unsigned stage=atomic_load_explicit(&monitor_stage,memory_order_relaxed);u64 stamp=atomic_load_explicit(&monitor_tick,memory_order_relaxed);
            struct fnv_vk_progress progress={0};fnv_vk_backend_progress(renderer,&progress);
            if(progress.submit_calls!=submits){submits=progress.submit_calls;last_submit=now;}
            u64 age=now>=stamp?armTicksToNs(now-stamp)/1000000:0;
            fprintf(output,"Watch elapsed_ms=%llu monitor=%s monitor_age_ms=%llu render_phase=%lu submit_calls=%llu fence_calls=%llu\n",(unsigned long long)(armTicksToNs(now-start)/1000000),stage<sizeof(names)/sizeof(*names)?names[stage]:"invalid",(unsigned long long)age,(unsigned long)progress.phase,(unsigned long long)progress.submit_calls,(unsigned long long)progress.wait_calls);
            int stalled=!fnv_engine_keyboard_is_open() && (age>=3000 || (submits>5000 && armTicksToNs(now-last_submit)>=10000000000ull));
            if(!stalled || !was_stalled)samples=0;
            if(stalled && samples<3){sample("monitor",monitor_handle);sample("guest-main",guest_handle);samples++;}
            was_stalled=stalled;
            fflush(output);last=now;
        }
        svcSleepThread(100000000);
    }
}
void fnv_engine_watchdog_stage(unsigned stage){atomic_store_explicit(&monitor_tick,armGetSystemTick(),memory_order_relaxed);atomic_store_explicit(&monitor_stage,stage,memory_order_relaxed);}
int fnv_engine_watchdog_start(uint32_t monitor,uint32_t guest){
    extern struct fnv_vk_backend *fnv_engine_watchdog_renderer(void);
    renderer=fnv_engine_watchdog_renderer();monitor_handle=monitor;guest_handle=guest;
    atomic_init(&monitor_stage,FNV_MON_START);atomic_init(&monitor_tick,armGetSystemTick());
    output=fopen("sdmc:/switch/FNV-nx/fnv-pc-engine-" FNV_ENGINE_HW_TAG "-watchdog.log","w");if(!output)return 0;
    if(!fnv_host_thread_create(&observer,watch,NULL,128u<<10)){fclose(output);output=NULL;return 0;}return 1;
}
