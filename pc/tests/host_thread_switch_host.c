/* No OS threads are launched: exercise syscall order and failure rollback. */
#include "host_thread.h"
#include <assert.h>
#include <stdio.h>
static unsigned step,fail_at,closed,started,set_calls,cases;
static uint64_t allowed,initial_mask;static int32_t ideal,chosen;static uint32_t mask;
Result threadCreate(Thread *t,void(*fn)(void*),void *arg,void *memory,size_t stack,int priority,int core){
    assert(fn && !arg && !memory && stack==65536 && priority==0x2c && core==-2);
    if(++step==fail_at)return 1;t->handle=123;return 0;
}
Result threadStart(Thread *t){assert(t->handle==123);started++;return ++step==fail_at?1:0;}
Result threadClose(Thread *t){assert(t->handle==123);closed++;return 0;}
Result threadWaitForExit(Thread *t){(void)t;return 0;}
Result svcGetInfo(uint64_t *out,int type,Handle process,uint64_t subtype){
    assert(type==InfoType_CoreMask && process==CUR_PROCESS_HANDLE && !subtype);
    if(++step==fail_at)return 1;*out=allowed;return 0;
}
Result svcGetThreadCoreMask(int32_t *core,uint64_t *out,Handle thread){
    assert(thread==123);if(++step==fail_at)return 1;*core=ideal;*out=initial_mask;return 0;
}
Result svcSetThreadCoreMask(Handle thread,int32_t core,uint32_t bits){
    assert(thread==123 && bits && !(bits&~allowed) && core>=0 && core<32 && (bits&(1u<<core)));
    set_calls++;chosen=core;mask=bits;return ++step==fail_at?1:0;
}
Result svcSetThreadPriority(Handle thread,uint32_t priority){(void)thread;(void)priority;return 0;}
Result svcGetThreadPriority(int32_t *priority,Handle thread){(void)thread;*priority=0x2c;return 0;}
void svcSleepThread(int64_t value){(void)value;}
static void entry(void *arg){(void)arg;assert(!"Synthetic thread must not run");}
static void setup(void){step=fail_at=closed=started=set_calls=0;mask=0;chosen=-1;allowed=7;initial_mask=4;ideal=2;cases++;}
int main(void){
    fnv_host_thread t;
    setup();assert(fnv_host_thread_create(&t,entry,NULL,65536));assert(set_calls==1 && chosen==2 && mask==7 && started==1 && !closed);
    setup();allowed=5;assert(fnv_host_thread_create(&t,entry,NULL,65536));assert(mask==5 && chosen==2);
    setup();allowed=1;ideal=2;assert(fnv_host_thread_create(&t,entry,NULL,65536));assert(mask==1 && chosen==0);
    setup();allowed=4;assert(fnv_host_thread_create(&t,entry,NULL,65536));assert(!set_calls && started==1);
    setup();initial_mask=7;assert(fnv_host_thread_create(&t,entry,NULL,65536));assert(!set_calls && started==1);
    setup();allowed=0;assert(!fnv_host_thread_create(&t,entry,NULL,65536));assert(closed==1 && !started);
    setup();allowed=UINT64_C(1)<<40;assert(!fnv_host_thread_create(&t,entry,NULL,65536));assert(closed==1 && !started);
    for(unsigned failure=1;failure<=5;failure++){
        setup();fail_at=failure;assert(!fnv_host_thread_create(&t,entry,NULL,65536));assert(closed==(failure!=1));assert(started==(failure==5));
    }
    printf("{\"status\":\"PASS\",\"affinity_and_failure_cases\":%u,\"actual_threads_started\":false}\n",cases);return 0;
}
