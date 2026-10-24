#ifndef FNV_BINK_STREAM_H
#define FNV_BINK_STREAM_H
#include "bink_decoder.h"
struct fnv_dsound8;
struct fnv_bink_stream;
enum fnv_bink_stream_result {FNV_BINK_STREAM_VIDEO,FNV_BINK_STREAM_AUDIO,FNV_BINK_STREAM_WAIT,FNV_BINK_STREAM_END,FNV_BINK_STREAM_ERROR};
struct fnv_bink_stream_stats {uint32_t pending_audio,paused,ended;uint64_t submitted_audio;};
/* One exclusive movie stream, serialized decoder/controller thread, including
 * lifecycle. Mixer and caller-owned I/O must outlive it. Caller must exclusively
 * own the mixer's movie input until close. Audio worker may pump concurrently.
 * open resets only the movie input. close drops pending movie input, but cannot
 * retract device/output queue audio. This is delivery, not a playback clock,
 * guest BINK ABI, texture upload or a claim of audible/device position. */
int fnv_bink_stream_open(const struct fnv_bink_io *,struct fnv_dsound8 *,struct fnv_bink_stream **,struct fnv_bink_info *);
/* One bounded decoder event or <=512 submitted PCM frames per call. WAIT means
 * paused or sink full; retry without sleeps here. Never advances the decoder
 * with unsubmitted PCM. VIDEO storage expires on the next unpaused step/close.
 * END means decoded EOF, not that audio already queued has finished playing. */
enum fnv_bink_stream_result fnv_bink_stream_step(struct fnv_bink_stream *,struct fnv_bink_frame *);
int fnv_bink_stream_pause(struct fnv_bink_stream *,int paused);
int fnv_bink_stream_stats(struct fnv_bink_stream *,struct fnv_bink_stream_stats *);
/* Valid close always releases the stream; returns whether mixer reset worked. */
int fnv_bink_stream_close(struct fnv_bink_stream *);
#endif
