#ifndef FNV_NATIVE_PE_TLS_H
#define FNV_NATIVE_PE_TLS_H
#include "pe_image.h"
#define FNV_PE_TLS_MAX_BYTES (1024u*1024u)
struct fnv_pe_tls;
struct fnv_pe_tls_info {uint32_t initialized_bytes,zero_bytes,alignment,index_address;};
/* Single EXE at preferred base. Snapshot template before any guest execution.
 * Nonempty callback arrays return UNRESOLVED until callback lifecycle exists.
 * Failure leaves image/index untouched and *out NULL; no TLS -> OK/NULL. */
enum fnv_native_result fnv_pe_tls_prepare(const struct fnv_native_image *,struct fnv_pe_tls **out);
void fnv_pe_tls_destroy(struct fnv_pe_tls *);
void fnv_pe_tls_describe(const struct fnv_pe_tls *,struct fnv_pe_tls_info *);
/* Caller owns mapped RW index storage and serializes process setup. */
void fnv_pe_tls_bind_index(struct fnv_pe_tls *);
/* Bounds checked before mutation. Host backing/lifetime/alignment belong to
 * caller; this function copies bytes, it does not map guest memory. */
int fnv_pe_tls_initialize(const struct fnv_pe_tls *,void *data,size_t bytes);
#endif
