#ifndef FNV_BINK_MOVIE_H
#define FNV_BINK_MOVIE_H
#include "windows_thread.h"
#include "bink_sound.h"
#include "bink_decoder.h"
struct fnv_dsound8;
struct fnv_bink_movie;
struct fnv_bink_movie_config {
    uint32_t arena_address,arena_bytes; /* dedicated RW arena, >=4096 bytes */
    struct fnv_dsound8 *sound;
    void *context;
    /* Resolve a guest ANSI path inside the game's sandbox, open read-only.
     * I/O context lives until close_io. Return actual file size, <=UINT32_MAX.
     * No arbitrary host path resolution is performed by the movie provider. */
    int (*open_io)(void *,const char *,struct fnv_bink_io *,uint64_t *);
    void (*close_io)(void *,struct fnv_bink_io *);
    int (*now_ns)(void *,uint64_t *);
    /* On audio backpressure: yield/pump host audio without invoking guest code.
     * Return zero if the device cannot progress. The worker must be live. */
    int (*wait_audio)(void *);
};
/* One movie, serialized guest/controller calls, same thread as decoder access.
 * Create before attach, destroy after all detach. Dedicated arena, mixer, I/O
 * callbacks and selection provider stay live; stop audio before destroying mixer.
 * Public header prefix is seven native uint32 fields. No external BINKSND ABI.
 * Only this provider's sound-open token is supported, not custom guest systems. */
enum fnv_cpu_result fnv_bink_movie_create(struct fnv_windows_process *,const struct fnv_bink_movie_config *,struct fnv_bink_movie **);
enum fnv_cpu_result fnv_bink_movie_bind_sound(struct fnv_bink_movie *,struct fnv_bink_sound *);
enum fnv_cpu_result fnv_bink_movie_destroy(struct fnv_bink_movie *);
/* Configure bink_sound.open_directsound with this factory and movie context.
 * Returns an actual guest-callable private opener that initializes the stream;
 * it is not interoperable with an external Bink DLL's BINKSND structure. */
uint32_t fnv_bink_movie_sound_factory(void *,uint32_t guest_device);
const struct fnv_cpu_export *fnv_bink_movie_exports(size_t *);
#endif
