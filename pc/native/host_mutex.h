#ifndef FNV_HOST_MUTEX_H
#define FNV_HOST_MUTEX_H
/* VM operations can zero large regions or enter the kernel. Block waiting
 * threads instead of spinning for the whole commit/permission transaction. */
#ifdef __SWITCH__
#include <switch/kernel/mutex.h>
typedef Mutex fnv_host_mutex;
static inline int fnv_host_mutex_init(fnv_host_mutex *m) {mutexInit(m);return 1;}
static inline void fnv_host_mutex_lock(fnv_host_mutex *m) {mutexLock(m);}
static inline void fnv_host_mutex_unlock(fnv_host_mutex *m) {mutexUnlock(m);}
static inline void fnv_host_mutex_destroy(fnv_host_mutex *m) {(void)m;}
#elif defined(_WIN32)
#include <windows.h>
typedef SRWLOCK fnv_host_mutex;
static inline int fnv_host_mutex_init(fnv_host_mutex *m) {InitializeSRWLock(m);return 1;}
static inline void fnv_host_mutex_lock(fnv_host_mutex *m) {AcquireSRWLockExclusive(m);}
static inline void fnv_host_mutex_unlock(fnv_host_mutex *m) {ReleaseSRWLockExclusive(m);}
static inline void fnv_host_mutex_destroy(fnv_host_mutex *m) {(void)m;}
#else
#include <pthread.h>
typedef pthread_mutex_t fnv_host_mutex;
static inline int fnv_host_mutex_init(fnv_host_mutex *m) {return pthread_mutex_init(m,NULL)==0;}
static inline void fnv_host_mutex_lock(fnv_host_mutex *m) {(void)pthread_mutex_lock(m);}
static inline void fnv_host_mutex_unlock(fnv_host_mutex *m) {(void)pthread_mutex_unlock(m);}
static inline void fnv_host_mutex_destroy(fnv_host_mutex *m) {(void)pthread_mutex_destroy(m);}
#endif
#endif
