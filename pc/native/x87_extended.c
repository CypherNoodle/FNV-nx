#include "x87_extended.h"
#if defined(__SWITCH__) && !defined(FNV_X87_THREAD_LOCAL_STATE)
#include <sys/lock.h>
#else
#include <stdatomic.h>
#endif
#include "platform.h"
#include "softfloat.h"

/* The default build serializes process-global SoftFloat state. The opt-in
 * private-state build requires EVERY SoftFloat object to use THREAD_LOCAL.
 * On Horizon use the newlib/libnx mutex: busy waiting can starve a lower
 * priority owner when guest workers fill the application's CPU cores.
 * Do not expose the underlying SoftFloat entry points to guest workers. */
#ifdef FNV_X87_THREAD_LOCAL_STATE
#ifndef THREAD_LOCAL
#error "Private SoftFloat state requires THREAD_LOCAL on all core objects"
#endif
static void acquire(void) {}
static void release(void) {}
#elif defined(__SWITCH__)
static _LOCK_T guard = __LOCK_INITIALIZER;
static void acquire(void) { __libc_lock_acquire(&guard); }
static void release(void) { __libc_lock_release(&guard); }
#else
static atomic_flag guard = ATOMIC_FLAG_INIT;
static void acquire(void) {
    while (atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {}
}
static void release(void) { atomic_flag_clear_explicit(&guard,memory_order_release); }
#endif
static const uint_fast8_t rounding[4]={softfloat_round_near_even,softfloat_round_min,softfloat_round_max,softfloat_round_minMag};
static uint16_t exception_bits(unsigned f) {
    return (uint16_t)(((f&softfloat_flag_invalid)?1:0) |
        ((f&softfloat_flag_infinite)?4:0) | ((f&softfloat_flag_overflow)?8:0) |
        ((f&softfloat_flag_underflow)?16:0) | ((f&softfloat_flag_inexact)?32:0));
}
struct fnv_x87_extended fnv_x87_unpack(const unsigned char bytes[10]) {
    struct fnv_x87_extended v = {0,0};
    for (unsigned i=0;i<8;i++) v.significand |= (uint64_t)bytes[i] << (8*i);
    v.sign_exponent = (uint16_t)(bytes[8] | (uint16_t)bytes[9]<<8);
    return v;
}
void fnv_x87_pack(unsigned char bytes[10], struct fnv_x87_extended value) {
    for (unsigned i=0;i<8;i++) bytes[i]=(unsigned char)(value.significand>>(8*i));
    bytes[8]=(unsigned char)value.sign_exponent;
    bytes[9]=(unsigned char)(value.sign_exponent>>8);
}
int fnv_x87_calculate(enum fnv_x87_operation operation, uint16_t control,
    struct fnv_x87_extended a, struct fnv_x87_extended b,
    struct fnv_x87_extended *result, uint16_t *raised) {
    unsigned pc=(control>>8)&3u, rc=(control>>10)&3u;
    if (!result || !raised || pc==1 || operation<FNV_X87_ADD || operation>FNV_X87_SQRT) return 0;
    extFloat80_t x,y,z;
    x.signif=a.significand;x.signExp=a.sign_exponent;
    y.signif=b.significand;y.signExp=b.sign_exponent;
    acquire();
    softfloat_roundingMode=rounding[rc];
    extF80_roundingPrecision=pc==0 ? 32 : pc==2 ? 64 : 80;
    softfloat_exceptionFlags=0;
    switch(operation) {
    case FNV_X87_ADD:z=extF80_add(x,y);break;
    case FNV_X87_SUB:z=extF80_sub(x,y);break;
    case FNV_X87_MUL:z=extF80_mul(x,y);break;
    case FNV_X87_DIV:z=extF80_div(x,y);break;
    default:z=extF80_sqrt(x);break;
    }
    unsigned f=(unsigned)softfloat_exceptionFlags;
    *result=(struct fnv_x87_extended){z.signif,z.signExp};
    *raised=exception_bits(f);
    release();
    return 1;
}
int fnv_x87_from_binary(unsigned width,uint64_t bits,struct fnv_x87_extended *result,uint16_t *raised) {
    if((width!=4 && width!=8) || !result || !raised)return 0;
    acquire();
    softfloat_exceptionFlags=0;
    extFloat80_t z=width==4 ? f32_to_extF80((float32_t){(uint32_t)bits}) : f64_to_extF80((float64_t){bits});
    unsigned denormal=width==4 ? !((uint32_t)bits&0x7f800000u) && ((uint32_t)bits&0x7fffffu) :
        !(bits&UINT64_C(0x7ff0000000000000)) && (bits&UINT64_C(0xfffffffffffff));
    *result=(struct fnv_x87_extended){z.signif,z.signExp};
    *raised=(uint16_t)(exception_bits((unsigned)softfloat_exceptionFlags)|(denormal?2:0));
    release();return 1;
}
int fnv_x87_to_binary(unsigned width,uint16_t control,struct fnv_x87_extended value,uint64_t *bits,uint16_t *raised) {
    if((width!=4 && width!=8) || !bits || !raised)return 0;
    extFloat80_t x;x.signif=value.significand;x.signExp=value.sign_exponent;
    acquire();
    softfloat_roundingMode=rounding[(control>>10)&3u];softfloat_exceptionFlags=0;
    *bits=width==4 ? (uint64_t)extF80_to_f32(x).v : extF80_to_f64(x).v;
    *raised=exception_bits((unsigned)softfloat_exceptionFlags);
    release();return 1;
}
int fnv_x87_compare(struct fnv_x87_extended a,struct fnv_x87_extended b,int quiet,int *order,uint16_t *raised) {
    if(!order || !raised || (quiet!=0 && quiet!=1))return 0;
    extFloat80_t x,y;x.signif=a.significand;x.signExp=a.sign_exponent;y.signif=b.significand;y.signExp=b.sign_exponent;
    unsigned nan=((a.sign_exponent&0x7fff)==0x7fff && (a.significand&UINT64_C(0x7fffffffffffffff))) ||
        ((b.sign_exponent&0x7fff)==0x7fff && (b.significand&UINT64_C(0x7fffffffffffffff)));
    acquire();
    softfloat_exceptionFlags=0;
    int equal=quiet ? extF80_eq(x,y) : extF80_eq_signaling(x,y);
    *order=nan ? 2 : equal ? 0 : extF80_lt(x,y) ? -1 : 1;
    *raised=exception_bits((unsigned)softfloat_exceptionFlags);
    release();return 1;
}
int fnv_x87_from_integer(unsigned width,uint64_t bits,struct fnv_x87_extended *result) {
    if(!result || (width!=2 && width!=4 && width!=8))return 0;
    if(width<8) {
        uint64_t mask=(UINT64_C(1)<<(width*8))-1;bits&=mask;
        if(bits&(UINT64_C(1)<<(width*8-1)))bits|=~mask;
    }
    int64_t integer=bits<=INT64_MAX ? (int64_t)bits : -1-(int64_t)(UINT64_MAX-bits);
    extFloat80_t value=i64_to_extF80(integer);
    *result=(struct fnv_x87_extended){value.signif,value.signExp};return 1;
}
int fnv_x87_to_integer(unsigned width,uint16_t control,struct fnv_x87_extended value,int truncate,uint64_t *bits,uint16_t *raised) {
    if(!bits || !raised || (width!=2 && width!=4 && width!=8) || (truncate!=0 && truncate!=1))return 0;
    extFloat80_t x;x.signif=value.significand;x.signExp=value.sign_exponent;
    acquire();
    uint_fast8_t mode=truncate ? softfloat_round_minMag : rounding[(control>>10)&3u];
    softfloat_exceptionFlags=0;
    int64_t n=width==8 ? extF80_to_i64(x,mode,true) : extF80_to_i32(x,mode,true);
    if(width==2 && (n<-32768 || n>32767 || (softfloat_exceptionFlags&softfloat_flag_invalid))) {
        n=-32768;softfloat_exceptionFlags=softfloat_flag_invalid;
    }
    *bits=(uint64_t)n;if(width<8)*bits&=(UINT64_C(1)<<(width*8))-1;
    *raised=exception_bits((unsigned)softfloat_exceptionFlags);
    release();return 1;
}
int fnv_x87_round_integer(uint16_t control,struct fnv_x87_extended value,struct fnv_x87_extended *result,uint16_t *raised) {
    if(!result || !raised)return 0;
    extFloat80_t x;x.signif=value.significand;x.signExp=value.sign_exponent;
    acquire();
    softfloat_exceptionFlags=0;
    extFloat80_t z=extF80_roundToInt(x,rounding[(control>>10)&3u],true);
    *result=(struct fnv_x87_extended){z.signif,z.signExp};*raised=exception_bits((unsigned)softfloat_exceptionFlags);
    release();return 1;
}
