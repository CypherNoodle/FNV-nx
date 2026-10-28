#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "x87_extended.h"
#include "softfloat.h"

static HANDLE start;
static volatile LONG failures;
static uintptr_t addresses[4][4];
static uint64_t expected[4];
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
static DWORD WINAPI worker(void *arg){
    unsigned rc=(unsigned)(uintptr_t)arg;
    addresses[rc][0]=(uintptr_t)&softfloat_roundingMode;
    addresses[rc][1]=(uintptr_t)&softfloat_exceptionFlags;
    addresses[rc][2]=(uintptr_t)&extF80_roundingPrecision;
    addresses[rc][3]=(uintptr_t)&softfloat_detectTininess;
    if(softfloat_roundingMode!=softfloat_round_near_even || softfloat_exceptionFlags || extF80_roundingPrecision!=80){
        InterlockedIncrement(&failures);return 0;
    }
    WaitForSingleObject(start,INFINITE);
    for(unsigned i=0;i<50000;i++)if(exercise(rc)!=expected[rc])InterlockedIncrement(&failures);
    return 0;
}
int main(void){
    HANDLE threads[4];LARGE_INTEGER a,b,hz;
    for(unsigned i=0;i<4;i++){expected[i]=exercise(i);if(!expected[i])return 2;}
    start=CreateEventA(NULL,TRUE,FALSE,NULL);if(!start)return 3;
    QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);
    for(unsigned i=0;i<4;i++){threads[i]=CreateThread(NULL,0,worker,(void*)(uintptr_t)i,0,NULL);if(!threads[i])return 4;}
    SetEvent(start);if(WaitForMultipleObjects(4,threads,TRUE,60000)!=WAIT_OBJECT_0)return 5;
    QueryPerformanceCounter(&b);
    for(unsigned i=0;i<4;i++)for(unsigned j=0;j<i;j++)for(unsigned k=0;k<4;k++)if(addresses[i][k]==addresses[j][k])failures++;
    for(unsigned i=0;i<4;i++)CloseHandle(threads[i]);CloseHandle(start);
    printf("cases=200000 failures=%ld elapsed_ms=%llu\n",failures,(unsigned long long)((b.QuadPart-a.QuadPart)*1000/hz.QuadPart));
    return failures!=0;
}
