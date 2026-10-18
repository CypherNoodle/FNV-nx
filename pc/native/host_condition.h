#ifndef FNV_HOST_CONDITION_H
#define FNV_HOST_CONDITION_H
#include <stdint.h>
#include "host_mutex.h"
/* All waits release/reacquire the supplied exclusive mutex. Return -1 on
 * error, 0 on timeout, 1 on notification (possibly spurious). Default 250ms
 * waits let native bridges observe a faulted peer without spinning. */
#ifdef __SWITCH__
#include <switch/kernel/condvar.h>
typedef CondVar fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){condvarInit(c);return 1;}
static inline int fnv_host_condition_wait_ms(fnv_host_condition *c,fnv_host_mutex *m,uint32_t ms){Result r=condvarWaitTimeout(c,m,(uint64_t)ms*1000000);return r==0?1:r==0xEA01?0:-1;}
static inline void fnv_host_condition_wake(fnv_host_condition *c){(void)condvarWakeOne(c);}
static inline void fnv_host_condition_wake_all(fnv_host_condition *c){(void)condvarWakeAll(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)c;}
#elif defined(_WIN32)
typedef CONDITION_VARIABLE fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){InitializeConditionVariable(c);return 1;}
static inline int fnv_host_condition_wait_ms(fnv_host_condition *c,fnv_host_mutex *m,uint32_t ms){return SleepConditionVariableSRW(c,m,ms,0)?1:GetLastError()==ERROR_TIMEOUT?0:-1;}
static inline void fnv_host_condition_wake(fnv_host_condition *c){WakeConditionVariable(c);}
static inline void fnv_host_condition_wake_all(fnv_host_condition *c){WakeAllConditionVariable(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)c;}
#else
#include <errno.h>
#include <time.h>
typedef pthread_cond_t fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){
    pthread_condattr_t a;if(pthread_condattr_init(&a))return 0;
    int r=pthread_condattr_setclock(&a,CLOCK_MONOTONIC);if(!r)r=pthread_cond_init(c,&a);
    (void)pthread_condattr_destroy(&a);return r==0;
}
static inline int fnv_host_condition_wait_ms(fnv_host_condition *c,fnv_host_mutex *m,uint32_t ms){
    struct timespec end;if(clock_gettime(CLOCK_MONOTONIC,&end))return -1;
    end.tv_sec+=ms/1000;end.tv_nsec+=(long)(ms%1000)*1000000;
    if(end.tv_nsec>=1000000000){end.tv_sec++;end.tv_nsec-=1000000000;}
    int r=pthread_cond_timedwait(c,m,&end);return r==0?1:r==ETIMEDOUT?0:-1;
}
static inline void fnv_host_condition_wake(fnv_host_condition *c){(void)pthread_cond_signal(c);}
static inline void fnv_host_condition_wake_all(fnv_host_condition *c){(void)pthread_cond_broadcast(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)pthread_cond_destroy(c);}
#endif
static inline int fnv_host_condition_wait(fnv_host_condition *c,fnv_host_mutex *m){return fnv_host_condition_wait_ms(c,m,250);}
#endif
