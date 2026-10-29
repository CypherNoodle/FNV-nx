#ifndef FNV_BINK_DECODER_H
#define FNV_BINK_DECODER_H
#include <stdint.h>
#include <stddef.h>
struct fnv_bink_decoder;
struct fnv_bink_io {
    void *context;
    int (*read)(void *,unsigned char *,int); /* bytes, zero EOF, negative error */
    int64_t (*seek)(void *,int64_t,int); /* SEEK_SET/CUR/END, or 0x10000=size */
};
struct fnv_bink_info {uint32_t width,height,frames,rate_num,rate_den,audio_rate,audio_channels,audio_tracks;};
enum fnv_bink_decode_result {FNV_BINK_VIDEO=0,FNV_BINK_AUDIO=1,FNV_BINK_END=2,FNV_BINK_DECODE_ERROR=3};
struct fnv_bink_frame {
    const unsigned char *rgba;uint32_t width,height,pitch;
    const int16_t *pcm;uint32_t audio_frames; /* stereo 48000 Hz */
    int64_t pts_us;
};
/* One serialized decoder thread for the initial no-threads FFmpeg profile.
 * Custom bounded I/O only, first audio track, mono/stereo, video <=1920x1080.
 * Caller owns I/O until close; exposed frame storage expires on next()/close().
 * No Bink guest handle ABI, scheduling, renderer upload or audio submission. */
int fnv_bink_decoder_open(const struct fnv_bink_io *,struct fnv_bink_decoder **,struct fnv_bink_info *);
enum fnv_bink_decode_result fnv_bink_decoder_next(struct fnv_bink_decoder *,struct fnv_bink_frame *);
void fnv_bink_decoder_close(struct fnv_bink_decoder *);
struct fnv_bink_decode_metrics {uint64_t video_frames,audio_frames,next_ns,convert_ns,io_ns,read_calls,read_bytes;};
void fnv_bink_decoder_metrics(struct fnv_bink_decode_metrics *);
#endif
