#ifndef FNV_D3D9_UNIFORM_STATE_H
#define FNV_D3D9_UNIFORM_STATE_H
#include "d3d9_constants.h"
#include "d3d9_uniform_pack.h"
#define FNV_UNIFORM_BUSY 0x8876021Cu
struct fnv_uniform_state;
struct fnv_uniform_config { uint32_t vertex_floats, pixel_floats, slots, max_bytes; };
struct fnv_uniform_draw {
    uint64_t ticket;
    const void *data[2];
    uint32_t bytes[2];
};
struct fnv_uniform_stats {
    uint64_t uploads, prepared, committed, retired, completed_serial;
    uint32_t free_slots, reserved_slots, submitted_slots;
};
/* CPU bank snapshots/staging versions, NOT a GPU buffer allocator. Methods
 * serialize internally; no external callbacks under the lock. Destroy needs
 * exclusive caller ownership. max_bytes must also fit the device UBO limit.
 */
uint32_t fnv_uniform_state_create(const struct fnv_uniform_config *,struct fnv_uniform_state **);
uint32_t fnv_uniform_state_destroy(struct fnv_uniform_state *);
/* Exactly the upload_constants callback signature. Copies temporary input;
 * rejects invalid ranges before mutation. BOOL bank retains raw API bits.
 */
int fnv_uniform_state_upload(void *,enum fnv_d3d9_stage,enum fnv_d3d9_constant_kind,
    uint32_t first,uint32_t count,const uint32_t *bits);
/* Copy/validate the plan at shader bind. NULL means no bound shader; a nonnull
 * zero-byte plan is a bound shader that does not use a UBO. Banks persist
 * across binds, even when COM has no dirty constants to upload again.
 */
uint32_t fnv_uniform_state_bind(struct fnv_uniform_state *,enum fnv_d3d9_stage,const struct fnv_uniform_plan *);
/* Atomically validate and copy selected VS/PS plans (mask bits 0/1).
 * A nonzero validate_only leaves bindings unchanged; no allocation or wait.
 * NULL selected plans unbind. Plans/entries must not alias private state. */
uint32_t fnv_uniform_state_bind_batch(struct fnv_uniform_state *,uint32_t mask,const struct fnv_uniform_plan *const plans[2],int validate_only);
/* Output unchanged on failure. On success, data is immutable until cancel
 * or a valid completion releases its ticket. Busy never spins or waits.
 */
uint32_t fnv_uniform_state_prepare(struct fnv_uniform_state *,struct fnv_uniform_draw *);
/* Temporary fixed VS bytes plus the live PS plan. Does not alter bindings or
 * programmable banks. Output/ticket semantics match prepare. */
uint32_t fnv_uniform_state_prepare_vertex(struct fnv_uniform_state *,const void *,uint32_t,struct fnv_uniform_draw *);
/* For ONE ordered queue. Commit only after a successful real submission,
 * with a unique strictly increasing serial for each submission. Use the
 * atomic batch method when one submission consumes several versions;
 * completion cannot interleave partway through publishing that batch.
 * Cancel only work
 * never submitted. Complete only with a serial whose real fence was observed;
 * EndScene is not evidence of GPU completion. Serial/ticket zero are invalid.
 */
uint32_t fnv_uniform_state_commit(struct fnv_uniform_state *,uint64_t ticket,uint64_t serial);
uint32_t fnv_uniform_state_commit_batch(struct fnv_uniform_state *,const uint64_t *tickets,uint32_t count,uint64_t serial);
uint32_t fnv_uniform_state_cancel(struct fnv_uniform_state *,uint64_t ticket);
uint32_t fnv_uniform_state_complete(struct fnv_uniform_state *,uint64_t serial);
uint32_t fnv_uniform_state_stats(struct fnv_uniform_state *,struct fnv_uniform_stats *);
#endif
