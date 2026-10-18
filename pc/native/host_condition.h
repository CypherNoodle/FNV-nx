#ifndef FNV_HOST_CONDITION_H
#define FNV_HOST_CONDITION_H
#include "host_mutex.h"
/* All waits release/reacquire the supplied exclusive mutex. Return -1 on
 * error, 0 after a 250ms timeout, 1 on notification (possibly spurious).
 * The timeout lets native bridges observe a faulted peer without spinning. */
#ifdef __SWITCH__
#include <switch/kernel/condvar.h>
typedef CondVar fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){condvarInit(c);return 1;}
static inline int fnv_host_condition_wait(fnv_host_condition *c,fnv_host_mutex *m){Result r=condvarWaitTimeout(c,m,250000000);return r==0?1:r==0xEA01?0:-1;}
static inline void fnv_host_condition_wake(fnv_host_condition *c){(void)condvarWakeOne(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)c;}
#elif defined(_WIN32)
typedef CONDITION_VARIABLE fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){InitializeConditionVariable(c);return 1;}
static inline int fnv_host_condition_wait(fnv_host_condition *c,fnv_host_mutex *m){return SleepConditionVariableSRW(c,m,250,0)?1:GetLastError()==ERROR_TIMEOUT?0:-1;}
static inline void fnv_host_condition_wake(fnv_host_condition *c){WakeConditionVariable(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)c;}
#else
#include <errno.h>
#include <time.h>
typedef pthread_cond_t fnv_host_condition;
static inline int fnv_host_condition_init(fnv_host_condition *c){return pthread_cond_init(c,NULL)==0;}
static inline int fnv_host_condition_wait(fnv_host_condition *c,fnv_host_mutex *m){
    struct timespec end;if(timespec_get(&end,TIME_UTC)!=TIME_UTC)return -1;
    end.tv_nsec+=250000000;if(end.tv_nsec>=1000000000){end.tv_sec++;end.tv_nsec-=1000000000;}
    int r=pthread_cond_timedwait(c,m,&end);return r==0?1:r==ETIMEDOUT?0:-1;
}
static inline void fnv_host_condition_wake(fnv_host_condition *c){(void)pthread_cond_signal(c);}
static inline void fnv_host_condition_destroy(fnv_host_condition *c){(void)pthread_cond_destroy(c);}
#endif
#endif
