#include "x87_extended.h"
#include <stdatomic.h>
#include "platform.h"
#include "softfloat.h"

/* SoftFloat is built with process-global state for this standalone core.
 * Every access goes through this lock and explicit guest control input.
 * Do not expose the underlying SoftFloat entry points to guest workers. */
static atomic_flag guard = ATOMIC_FLAG_INIT;
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
    while (atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {}
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
    atomic_flag_clear_explicit(&guard,memory_order_release);
    return 1;
}
int fnv_x87_from_binary(unsigned width,uint64_t bits,struct fnv_x87_extended *result,uint16_t *raised) {
    if((width!=4 && width!=8) || !result || !raised)return 0;
    while(atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {}
    softfloat_exceptionFlags=0;
    extFloat80_t z=width==4 ? f32_to_extF80((float32_t){(uint32_t)bits}) : f64_to_extF80((float64_t){bits});
    unsigned denormal=width==4 ? !((uint32_t)bits&0x7f800000u) && ((uint32_t)bits&0x7fffffu) :
        !(bits&UINT64_C(0x7ff0000000000000)) && (bits&UINT64_C(0xfffffffffffff));
    *result=(struct fnv_x87_extended){z.signif,z.signExp};
    *raised=(uint16_t)(exception_bits((unsigned)softfloat_exceptionFlags)|(denormal?2:0));
    atomic_flag_clear_explicit(&guard,memory_order_release);return 1;
}
int fnv_x87_to_binary(unsigned width,uint16_t control,struct fnv_x87_extended value,uint64_t *bits,uint16_t *raised) {
    if((width!=4 && width!=8) || !bits || !raised)return 0;
    extFloat80_t x;x.signif=value.significand;x.signExp=value.sign_exponent;
    while(atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {}
    softfloat_roundingMode=rounding[(control>>10)&3u];softfloat_exceptionFlags=0;
    *bits=width==4 ? (uint64_t)extF80_to_f32(x).v : extF80_to_f64(x).v;
    *raised=exception_bits((unsigned)softfloat_exceptionFlags);
    atomic_flag_clear_explicit(&guard,memory_order_release);return 1;
}
