#ifndef FNV_BINK_SOUND_H
#define FNV_BINK_SOUND_H
#include "windows_thread.h"
struct fnv_bink_sound;
struct fnv_bink_sound_state {uint32_t factory,parameter,open;};
struct fnv_bink_sound_config {
    void *context;
    /* Optional real host sound-system factory. Returns a guest callable sound
     * open entry, or zero on actual initialization failure. Never a host ptr.
     * Missing factory stops explicitly; it is not treated as silent playback. */
    uint32_t (*open_directsound)(void *,uint32_t guest_device);
};
/* Setup before attach, destroy after all detach. Config/context stay alive.
 * Selection only: this is not a decoder, PCM mixer or BINKSND layout bridge. */
enum fnv_cpu_result fnv_bink_sound_create(struct fnv_windows_process *,const struct fnv_bink_sound_config *,struct fnv_bink_sound **);
enum fnv_cpu_result fnv_bink_sound_destroy(struct fnv_bink_sound *);
enum fnv_cpu_result fnv_bink_sound_state(struct fnv_bink_sound *,struct fnv_bink_sound_state *);
const struct fnv_cpu_export *fnv_bink_sound_exports(size_t *);
#endif
