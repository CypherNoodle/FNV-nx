/* Own serialized decoder-to-DirectSound movie delivery with bounded progress. */
#include "bink_stream.h"
#include "dsound8.h"
#include <stdlib.h>
#include <string.h>
struct fnv_bink_stream {
    struct fnv_bink_decoder *decoder;struct fnv_dsound8 *sound;
    struct fnv_bink_frame pending;uint32_t offset,paused,ended,failed;
    uint64_t submitted;
};
static struct fnv_bink_stream *active;
int fnv_bink_stream_open(const struct fnv_bink_io *io,struct fnv_dsound8 *sound,struct fnv_bink_stream **out,struct fnv_bink_info *info){
    if(!out)return 0;
    *out=NULL;struct fnv_dsound_movie_stats stats;
    if(active || !info || fnv_dsound8_movie_stats(sound,&stats)!=FNV_PCM_OK)return 0;
    struct fnv_bink_stream *s=calloc(1,sizeof(*s));if(!s)return 0;
    struct fnv_bink_info decoded;
    if(!fnv_bink_decoder_open(io,&s->decoder,&decoded)){free(s);return 0;}
    if(fnv_dsound8_movie_reset(sound)!=FNV_PCM_OK){fnv_bink_decoder_close(s->decoder);free(s);return 0;}
    s->sound=sound;active=s;*info=decoded;*out=s;return 1;
}
enum fnv_bink_stream_result fnv_bink_stream_step(struct fnv_bink_stream *s,struct fnv_bink_frame *out){
    if(!out)return FNV_BINK_STREAM_ERROR;
    memset(out,0,sizeof(*out));
    if(!s || s!=active || s->failed)return FNV_BINK_STREAM_ERROR;
    if(s->ended)return FNV_BINK_STREAM_END;
    if(s->paused)return FNV_BINK_STREAM_WAIT;
    if(!s->pending.audio_frames){
        enum fnv_bink_decode_result r=fnv_bink_decoder_next(s->decoder,&s->pending);s->offset=0;
        if(r==FNV_BINK_VIDEO){*out=s->pending;s->pending.audio_frames=0;return FNV_BINK_STREAM_VIDEO;}
        if(r==FNV_BINK_END){s->ended=1;return FNV_BINK_STREAM_END;}
        if(r!=FNV_BINK_AUDIO || !s->pending.pcm || !s->pending.audio_frames || s->pending.audio_frames>262144)goto bad;
    }
    uint32_t count=s->pending.audio_frames-s->offset;if(count>512)count=512;
    enum fnv_pcm_result written=fnv_dsound8_movie_write(s->sound,s->pending.pcm+(size_t)s->offset*2,count);
    if(written==FNV_PCM_FULL)return FNV_BINK_STREAM_WAIT;
    if(written!=FNV_PCM_OK)goto bad;
    s->offset+=count;s->submitted+=count;
    if(s->offset==s->pending.audio_frames)s->pending.audio_frames=s->offset=0;
    return FNV_BINK_STREAM_AUDIO;
bad:s->failed=1;return FNV_BINK_STREAM_ERROR;
}
int fnv_bink_stream_pause(struct fnv_bink_stream *s,int paused){
    if(!s || s!=active || s->failed || (paused!=0&&paused!=1))return 0;
    if(fnv_dsound8_movie_pause(s->sound,paused)!=FNV_PCM_OK){s->failed=1;return 0;}
    s->paused=(uint32_t)paused;return 1;
}
int fnv_bink_stream_stats(struct fnv_bink_stream *s,struct fnv_bink_stream_stats *out){
    if(!s || s!=active || !out)return 0;
    *out=(struct fnv_bink_stream_stats){s->pending.audio_frames-s->offset,s->paused,s->ended,s->submitted};return 1;
}
int fnv_bink_stream_close(struct fnv_bink_stream *s){
    if(!s || s!=active)return 0;
    int reset=fnv_dsound8_movie_reset(s->sound)==FNV_PCM_OK;
    fnv_bink_decoder_close(s->decoder);active=NULL;free(s);return reset;
}
