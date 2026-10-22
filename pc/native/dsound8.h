#ifndef FNV_DSOUND8_H
#define FNV_DSOUND8_H
#include "windows_thread.h"
#include "pcm_queue.h"
struct fnv_dsound8;
/* Owns a dedicated guest arena (>=8192 RW bytes), disjoint from all guest
 * arenas, until destroy. First4096 bytes contain COM tables/identities; the
 * remainder stages Lock writes. Retired identities/storage are not recycled.
 * Dedicated output queue must outlive provider and audout, capacity>=512.
 * Lifecycle serialized before attach/after detach; stop audout before destroy. */
struct fnv_dsound8_config {
    uint32_t arena_address,arena_bytes;int initial_focus;
    struct fnv_guest_memory *memory;
    struct fnv_pcm_queue *output;
    void *context;int (*valid_window)(void *,uint32_t);
};
enum fnv_cpu_result fnv_dsound8_create(struct fnv_windows_process *,const struct fnv_dsound8_config *,struct fnv_dsound8 **);
enum fnv_cpu_result fnv_dsound8_destroy(struct fnv_dsound8 *);
const struct fnv_cpu_export *fnv_dsound8_exports(size_t *);
/* Host feeds current app focus; no HID/window/guest calls on the audio worker.
 * Default buffers mute without focus while cursors continue; GLOBALFOCUS stays
 * audible. Sticky-focus policy is explicitly unsupported. */
enum fnv_cpu_result fnv_dsound8_set_focus(struct fnv_dsound8 *,int focused);
/* Pull producer for the existing PCM/audout pipeline, frames1..512. No guest
 * calls or guest memory reads. FULL rolls back every playback cursor. */
enum fnv_pcm_result fnv_dsound8_pump(struct fnv_dsound8 *,uint32_t frames);
/* Callback compatible with fnv_horizon_audio_open_source; zero=success. */
uint32_t fnv_dsound8_produce(void *,uint32_t frames);
#endif
