#ifndef FNV_DSOUND_STREAMS_H
#define FNV_DSOUND_STREAMS_H
#include "dsound8.h"
/* Eight independent bounded music streams; stereo s16/48 kHz. Tokens carry
 * generations. Lifecycle is serialized; output worker never calls guest code. */
struct fnv_dsound_stream_stats {uint32_t queued,capacity,paused;uint64_t played;};
enum fnv_pcm_result fnv_dsound_stream_open(struct fnv_dsound8 *,uint32_t *);
enum fnv_pcm_result fnv_dsound_stream_close(struct fnv_dsound8 *,uint32_t);
enum fnv_pcm_result fnv_dsound_stream_write(struct fnv_dsound8 *,uint32_t,const int16_t *,uint32_t);
enum fnv_pcm_result fnv_dsound_stream_control(struct fnv_dsound8 *,uint32_t,int paused,int32_t volume,int32_t balance);
enum fnv_pcm_result fnv_dsound_stream_reset(struct fnv_dsound8 *,uint32_t);
enum fnv_pcm_result fnv_dsound_stream_stats(struct fnv_dsound8 *,uint32_t,struct fnv_dsound_stream_stats *);
#endif
