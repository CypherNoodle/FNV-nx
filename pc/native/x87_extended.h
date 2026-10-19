#ifndef FNV_X87_EXTENDED_H
#define FNV_X87_EXTENDED_H
#include <stdint.h>
/* Exact extended-real payload; C struct padding is never guest memory. */
struct fnv_x87_extended { uint64_t significand; uint16_t sign_exponent; };
enum fnv_x87_operation { FNV_X87_ADD, FNV_X87_SUB, FNV_X87_MUL, FNV_X87_DIV, FNV_X87_SQRT };
struct fnv_x87_extended fnv_x87_unpack(const unsigned char bytes[10]);
void fnv_x87_pack(unsigned char bytes[10], struct fnv_x87_extended value);
/* Arithmetic core only: raised returns x87 IE/ZE/OE/UE/PE bits. This does not
 * implement denormal-operand signalling, C1, stack tags or guest exceptions.
 * Returns zero for reserved precision or an invalid operation, without writes.
 * The caller must handle unmasked exceptions before publishing the result. */
int fnv_x87_calculate(enum fnv_x87_operation operation, uint16_t control,
    struct fnv_x87_extended a, struct fnv_x87_extended b,
    struct fnv_x87_extended *result, uint16_t *raised);
int fnv_x87_from_binary(unsigned width,uint64_t bits,struct fnv_x87_extended *result,uint16_t *raised);
int fnv_x87_to_binary(unsigned width,uint16_t control,struct fnv_x87_extended value,uint64_t *bits,uint16_t *raised);
int fnv_x87_compare(struct fnv_x87_extended a,struct fnv_x87_extended b,int quiet,int *order,uint16_t *raised);
int fnv_x87_from_integer(unsigned width,uint64_t bits,struct fnv_x87_extended *result);
int fnv_x87_to_integer(unsigned width,uint16_t control,struct fnv_x87_extended value,int truncate,uint64_t *bits,uint16_t *raised);
int fnv_x87_round_integer(uint16_t control,struct fnv_x87_extended value,struct fnv_x87_extended *result,uint16_t *raised);
#endif
