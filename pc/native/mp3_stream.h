#ifndef FNV_MP3_STREAM_H
#define FNV_MP3_STREAM_H
#include <stddef.h>
#include <stdint.h>
#include "pcm_queue.h"
struct fnv_mp3;
enum fnv_mp3_result {FNV_MP3_OK,FNV_MP3_EOF,FNV_MP3_AGAIN,FNV_MP3_INVALID,FNV_MP3_IO,FNV_MP3_FORMAT,FNV_MP3_NO_MEMORY,FNV_MP3_BUSY};
struct fnv_mp3_io {void *context;int (*read)(void *,void *,size_t,size_t *);int (*seek)(void *,uint64_t);};
struct fnv_mp3_info {uint32_t source_rate,source_channels;uint64_t source_frames,output_frames;};
/* Bounded streaming, no entire-track allocation/index. Context retained until
 * close, callbacks return 1=success (read=0 bytes means EOF), 0=IO failure.
 * File API accepts a host-validated path, never an unchecked guest path.
 * One owner; no concurrent read/pump/rewind/close. */
enum fnv_mp3_result fnv_mp3_open(const struct fnv_mp3_io *,struct fnv_mp3 **);
enum fnv_mp3_result fnv_mp3_open_file(const char *,struct fnv_mp3 **);
void fnv_mp3_close(struct fnv_mp3 *);
enum fnv_mp3_result fnv_mp3_info(struct fnv_mp3 *,struct fnv_mp3_info *);
enum fnv_mp3_result fnv_mp3_read(struct fnv_mp3 *,int16_t *stereo,uint32_t capacity,uint32_t *frames);
/* Retains decoded pending block on FULL; AGAIN means retry after consumer.
 * Do not interleave read with a pending pump. No blocking/busy retry inside. */
enum fnv_mp3_result fnv_mp3_pump(struct fnv_mp3 *,struct fnv_pcm_queue *);
enum fnv_mp3_result fnv_mp3_rewind(struct fnv_mp3 *);
#endif
