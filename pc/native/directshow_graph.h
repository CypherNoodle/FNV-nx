#ifndef FNV_DIRECTSHOW_GRAPH_H
#define FNV_DIRECTSHOW_GRAPH_H
#include "windows_thread.h"
struct fnv_dsound8;
struct fnv_directshow;
struct fnv_directshow_config {
    uint32_t arena_address,arena_bytes; /* dedicated RW arena, >=0x90000 */
    struct fnv_guest_memory *memory;
    struct fnv_dsound8 *sound;
};
/* Bounded pull-MP3 graph. Each graph belongs to its creating guest thread;
 * guest IAsyncReader callbacks execute only on that thread inside an execute
 * boundary. No guest calls from the audout worker. Eight live graphs, 512
 * retired interface identities. Setup before attach, teardown after detach.
 * General DirectShow filter discovery, video and arbitrary connections are
 * unsupported. Destroy requires all graph references released by the guest. */
enum fnv_cpu_result fnv_directshow_create(struct fnv_windows_process *,const struct fnv_directshow_config *,struct fnv_directshow **);
enum fnv_cpu_result fnv_directshow_destroy(struct fnv_directshow *);
#endif
