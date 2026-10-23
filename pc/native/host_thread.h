#ifndef FNV_HOST_THREAD_H
#define FNV_HOST_THREAD_H
/* Own the native joinable thread until join. This is not a guest HANDLE. */
#include <stddef.h>
#include <stdint.h>
typedef void (*fnv_host_thread_entry)(void *);
#ifdef __SWITCH__
#include <switch/kernel/thread.h>
#include <switch/kernel/svc.h>
#include <switch/result.h>
typedef struct {Thread thread;} fnv_host_thread;
static inline int fnv_host_thread_create(fnv_host_thread *t,fnv_host_thread_entry fn,void *arg,size_t stack){
    if(R_FAILED(threadCreate(&t->thread,fn,arg,NULL,stack,0x2c,-2)))return 0;
    if(R_FAILED(threadStart(&t->thread))){(void)threadClose(&t->thread);return 0;}return 1;
}
static inline int fnv_host_thread_join(fnv_host_thread *t){
    if(R_FAILED(threadWaitForExit(&t->thread)))return 0;
    return R_SUCCEEDED(threadClose(&t->thread));
}
static inline void fnv_host_thread_sleep(unsigned ms){svcSleepThread((int64_t)ms*1000000);}
#elif defined(_WIN32)
#include <windows.h>
typedef struct {HANDLE handle;fnv_host_thread_entry entry;void *context;} fnv_host_thread;
static DWORD WINAPI fnv_host_thread_trampoline(void *arg){fnv_host_thread *t=arg;t->entry(t->context);return 0;}
static inline int fnv_host_thread_create(fnv_host_thread *t,fnv_host_thread_entry fn,void *arg,size_t stack){
    t->entry=fn;t->context=arg;t->handle=CreateThread(NULL,stack,fnv_host_thread_trampoline,t,0,NULL);return t->handle!=NULL;
}
static inline int fnv_host_thread_join(fnv_host_thread *t){
    if(WaitForSingleObject(t->handle,INFINITE)!=WAIT_OBJECT_0)return 0;
    return CloseHandle(t->handle)!=0;
}
static inline void fnv_host_thread_sleep(unsigned ms){Sleep(ms);}
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
typedef struct {pthread_t thread;fnv_host_thread_entry entry;void *context;} fnv_host_thread;
static void *fnv_host_thread_trampoline(void *arg){fnv_host_thread *t=arg;t->entry(t->context);return NULL;}
static inline int fnv_host_thread_create(fnv_host_thread *t,fnv_host_thread_entry fn,void *arg,size_t stack){
    pthread_attr_t attributes;if(pthread_attr_init(&attributes))return 0;
    if(pthread_attr_setstacksize(&attributes,stack)){pthread_attr_destroy(&attributes);return 0;}
    t->entry=fn;t->context=arg;int result=pthread_create(&t->thread,&attributes,fnv_host_thread_trampoline,t);
    pthread_attr_destroy(&attributes);return result==0;
}
static inline int fnv_host_thread_join(fnv_host_thread *t){return pthread_join(t->thread,NULL)==0;}
static inline void fnv_host_thread_sleep(unsigned ms){
    if(!ms){sched_yield();return;}
    struct timespec span={(time_t)(ms/1000),(long)(ms%1000)*1000000};
    while(nanosleep(&span,&span) && errno==EINTR){}
}
#endif
/* Bounded relative levels -2..2. Horizon uses the application's 0x2c base;
 * lower native numbers run first. This is policy, not Windows priority-class
 * emulation. POSIX SCHED_OTHER cannot express these levels: report unsupported. */
static inline uint32_t fnv_host_thread_priority(fnv_host_thread *t,int set,int32_t *level){
#ifdef __SWITCH__
    Handle handle=t?t->thread.handle:CUR_THREAD_HANDLE;
    if(set)return R_SUCCEEDED(svcSetThreadPriority(handle,0x2c-*level*2))?0:5;
    int32_t priority;Result r=svcGetThreadPriority(&priority,handle);if(R_FAILED(r))return 5;
    int32_t delta=0x2c-priority;
    if(delta<-4 || delta>4 || delta%2)return 50;
    *level=delta/2;return 0;
#elif defined(_WIN32)
    HANDLE handle=t?t->handle:GetCurrentThread();
    if(set)return SetThreadPriority(handle,*level)?0:GetLastError();
    int value=GetThreadPriority(handle);if(value==THREAD_PRIORITY_ERROR_RETURN)return GetLastError();
    *level=value;return 0;
#else
    (void)t;(void)set;(void)level;return 50;
#endif
}
#endif
