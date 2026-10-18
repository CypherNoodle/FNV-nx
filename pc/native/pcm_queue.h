#ifndef FNV_PCM_QUEUE_H
#define FNV_PCM_QUEUE_H
#include <stdint.h>
#include <stddef.h>
struct fnv_pcm_queue;
/* Interleaved stereo signed 16-bit, 48000 Hz. Capacity in stereo frames.
 * Bounded queue, no allocation during write/read; destroy after producer and
 * consumer stop. One mutex permits multiple host producers without spin waits. */
enum fnv_pcm_result {FNV_PCM_OK,FNV_PCM_INVALID,FNV_PCM_FULL,FNV_PCM_NO_MEMORY};
struct fnv_pcm_stats {uint32_t queued,capacity;uint64_t written,read,silence,full_writes;};
enum fnv_pcm_result fnv_pcm_create(uint32_t capacity,struct fnv_pcm_queue **);
void fnv_pcm_destroy(struct fnv_pcm_queue *);
/* Atomic write: FULL preserves every input frame for caller retry. */
enum fnv_pcm_result fnv_pcm_write(struct fnv_pcm_queue *,const int16_t *,uint32_t frames);
/* Returns source frames; fills remaining requested frames with zero. */
uint32_t fnv_pcm_read(struct fnv_pcm_queue *,int16_t *,uint32_t frames);
enum fnv_pcm_result fnv_pcm_stats(struct fnv_pcm_queue *,struct fnv_pcm_stats *);
#endif
