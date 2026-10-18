#ifndef FNV_MEDIA_HORIZON_H
#define FNV_MEDIA_HORIZON_H
#include "xinput.h"
#include "pcm_queue.h"
#ifdef __SWITCH__
struct fnv_horizon_pad;
struct fnv_horizon_audio;
/* App owns HID initialization; open before attach, close after all detach.
 * labels=1 maps Nintendo A/B/X/Y by printed label, 0 maps physical Xbox layout.
 * poll runs on one host thread once per frame, never in a guest GetState call. */
enum fnv_cpu_result fnv_horizon_pad_open(struct fnv_windows_process *,int labels,struct fnv_horizon_pad **);
enum fnv_cpu_result fnv_horizon_pad_poll(struct fnv_horizon_pad *);
enum fnv_cpu_result fnv_horizon_pad_close(struct fnv_horizon_pad *);
/* Owns an audout session and dedicated event-driven worker. Caller retains the
 * queue until close. No other audout owner may run concurrently. No decoding,
 * resampling or DirectSound ABI is implied by this PCM output backend. */
uint32_t fnv_horizon_audio_open(struct fnv_pcm_queue *,struct fnv_horizon_audio **);
uint32_t fnv_horizon_audio_error(struct fnv_horizon_audio *);
/* Nonzero close result retains object/queue ownership; caller must not free it. */
uint32_t fnv_horizon_audio_close(struct fnv_horizon_audio *);
#endif
#endif
