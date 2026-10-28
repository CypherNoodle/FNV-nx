/* Exercise the Horizon lock branch through a Windows blocking-mutex shim.
 * This checks shared SoftFloat state, not Horizon scheduling behavior. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "x87_extended.h"
#include <sys/lock.h>

static volatile LONG held, failures, acquisitions;
void __libc_lock_acquire(_LOCK_T *lock) {
    AcquireSRWLockExclusive(lock);
    if(InterlockedIncrement(&held)!=1)InterlockedIncrement(&failures);
    /* The first 24 acquisitions establish serial expectations. Stall the
     * first worker's owner so the remaining workers must contend. */
    if(InterlockedIncrement(&acquisitions)==25)Sleep(10);
}
void __libc_lock_release(_LOCK_T *lock) {
    if(InterlockedDecrement(&held)!=0)InterlockedIncrement(&failures);
    ReleaseSRWLockExclusive(lock);
}
static uint64_t mix(uint64_t h,uint64_t n){return (h^n)*UINT64_C(1099511628211);}
static uint64_t exercise(unsigned rc){
    uint16_t cw=(uint16_t)(0x037f|(rc<<10)),flags;
    struct fnv_x87_extended one,three,value,rounded;
    uint64_t bits,h=1;int order;
    if(!fnv_x87_from_integer(4,1,&one) || !fnv_x87_from_integer(4,3,&three))return 0;
    if(!fnv_x87_calculate(FNV_X87_DIV,cw,one,three,&value,&flags))return 0;
    h=mix(mix(mix(h,value.significand),value.sign_exponent),flags);
    if(!fnv_x87_to_binary(4,cw,value,&bits,&flags))return 0;
    h=mix(mix(h,bits),flags);
    if(!fnv_x87_from_binary(4,bits,&rounded,&flags))return 0;
    h=mix(mix(mix(h,rounded.significand),rounded.sign_exponent),flags);
    if(!fnv_x87_compare(value,rounded,1,&order,&flags))return 0;
    h=mix(mix(h,(unsigned)order),flags);
    if(!fnv_x87_to_integer(4,cw,value,0,&bits,&flags))return 0;
    h=mix(mix(h,bits),flags);
    if(!fnv_x87_round_integer(cw,value,&rounded,&flags))return 0;
    return mix(mix(mix(h,rounded.significand),rounded.sign_exponent),flags);
}
static uint64_t expected[4];
static HANDLE start;
static DWORD WINAPI worker(void *arg){
    unsigned rc=(unsigned)(uintptr_t)arg;
    WaitForSingleObject(start,INFINITE);
    for(unsigned i=0;i<20000;i++)if(exercise(rc)!=expected[rc])InterlockedIncrement(&failures);
    return 0;
}
int main(void){
    HANDLE threads[4];
    for(unsigned i=0;i<4;i++){expected[i]=exercise(i);if(!expected[i])return 2;}
    start=CreateEventA(NULL,TRUE,FALSE,NULL);if(!start)return 3;
    for(unsigned i=0;i<4;i++){threads[i]=CreateThread(NULL,0,worker,(void*)(uintptr_t)i,0,NULL);if(!threads[i])return 4;}
    SetEvent(start);
    if(WaitForMultipleObjects(4,threads,TRUE,60000)!=WAIT_OBJECT_0)return 5;
    for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);CloseHandle(start);
    printf("cases=80000 acquisitions=%ld failures=%ld held=%ld\n",acquisitions,failures,held);
    return failures || held || acquisitions!=480024;
}
