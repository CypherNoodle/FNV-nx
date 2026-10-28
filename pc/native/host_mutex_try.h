#ifndef FNV_HOST_MUTEX_TRY_H
#define FNV_HOST_MUTEX_TRY_H
#include "host_mutex.h"
#ifdef __SWITCH__
static inline int fnv_host_mutex_trylock(fnv_host_mutex *m){return mutexTryLock(m);}
#elif defined(_WIN32)
static inline int fnv_host_mutex_trylock(fnv_host_mutex *m){return TryAcquireSRWLockExclusive(m)!=0;}
#else
static inline int fnv_host_mutex_trylock(fnv_host_mutex *m){return pthread_mutex_trylock(m)==0;}
#endif
#endif
