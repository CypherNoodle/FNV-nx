/* Own bounded FFmpeg adapter; external decoder sources remain unchanged. */
#include "bink_decoder.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "host_clock.h"
static atomic_uint_fast64_t video_count,audio_count,next_time,convert_time,io_time,read_count,read_bytes;
static void metric_elapsed(atomic_uint_fast64_t *counter,uint64_t start){uint64_t end;if(start && fnv_host_now_ns(&end) && end>=start)atomic_fetch_add_explicit(counter,end-start,memory_order_relaxed);}
void fnv_bink_decoder_metrics(struct fnv_bink_decode_metrics *out){if(!out)return;
    *out=(struct fnv_bink_decode_metrics){atomic_load_explicit(&video_count,memory_order_relaxed),atomic_load_explicit(&audio_count,memory_order_relaxed),
        atomic_load_explicit(&next_time,memory_order_relaxed),atomic_load_explicit(&convert_time,memory_order_relaxed),atomic_load_explicit(&io_time,memory_order_relaxed),atomic_load_explicit(&read_count,memory_order_relaxed),atomic_load_explicit(&read_bytes,memory_order_relaxed)};}
struct fnv_bink_decoder {
    struct fnv_bink_io input;struct fnv_bink_info info;
    AVFormatContext *format;AVIOContext *io;AVCodecContext *codec[2];
    AVFrame *frame;AVPacket *packet;struct SwsContext *scale;struct SwrContext *resample;
    int stream[2],current,eof,flushed[2],failed;unsigned char *rgba;int16_t *pcm;
    uint32_t pcm_capacity;uint64_t audio_position;AVRational video_timebase;
};
static int read_input(void *context,uint8_t *data,int bytes){
    struct fnv_bink_decoder *d=context;uint64_t start=0;(void)fnv_host_now_ns(&start);int n=d->input.read(d->input.context,data,bytes);
    metric_elapsed(&io_time,start);atomic_fetch_add_explicit(&read_count,1,memory_order_relaxed);if(n>0)atomic_fetch_add_explicit(&read_bytes,(uint64_t)n,memory_order_relaxed);
    return n>bytes || n<0?AVERROR(EIO):n?n:AVERROR_EOF;
}
static int64_t seek_input(void *context,int64_t offset,int whence){
    struct fnv_bink_decoder *d=context;int64_t result=d->input.seek(d->input.context,offset,whence&~AVSEEK_FORCE);
    return result<0?AVERROR(EIO):result;
}
void fnv_bink_decoder_close(struct fnv_bink_decoder *d){
    if(!d)return;
    avcodec_free_context(d->codec);avcodec_free_context(d->codec+1);
    av_frame_free(&d->frame);av_packet_free(&d->packet);sws_freeContext(d->scale);swr_free(&d->resample);
    avformat_close_input(&d->format);
    if(d->io){av_freep(&d->io->buffer);avio_context_free(&d->io);}
    av_free(d->rgba);av_free(d->pcm);free(d);
}
int fnv_bink_decoder_open(const struct fnv_bink_io *input,struct fnv_bink_decoder **out,struct fnv_bink_info *info){
    if(!out)return 0;
    *out=NULL;if(!input || !input->read || !input->seek || !info)return 0;
    struct fnv_bink_decoder *d=calloc(1,sizeof(*d));if(!d)return 0;
    d->input=*input;d->stream[0]=d->stream[1]=-1;d->current=-1;
    unsigned char *buffer=av_malloc(32768);if(!buffer)goto bad;
    d->io=avio_alloc_context(buffer,32768,0,d,read_input,NULL,seek_input);if(!d->io){av_free(buffer);goto bad;}
    d->format=avformat_alloc_context();if(!d->format)goto bad;
    d->format->pb=d->io;d->format->flags|=AVFMT_FLAG_CUSTOM_IO;d->format->max_streams=9;
    const AVInputFormat *format=av_find_input_format("bink");if(!format || avformat_open_input(&d->format,NULL,format,NULL)<0)goto bad;
    for(unsigned i=0;i<d->format->nb_streams;i++){
        AVStream *s=d->format->streams[i];AVCodecParameters *p=s->codecpar;
        if(p->codec_type==AVMEDIA_TYPE_VIDEO){
            if(d->stream[0]>=0 || p->codec_id!=AV_CODEC_ID_BINKVIDEO || p->width<1 || p->height<1 || p->width>1920 || p->height>1080 || s->duration<1 || s->duration>1000000 || s->time_base.num<1 || s->time_base.den<1)goto bad;
            d->stream[0]=(int)i;d->info.width=p->width;d->info.height=p->height;d->info.frames=(uint32_t)s->duration;
            d->info.rate_num=s->time_base.den;d->info.rate_den=s->time_base.num;d->video_timebase=s->time_base;
        }else if(p->codec_type==AVMEDIA_TYPE_AUDIO){
            d->info.audio_tracks++;
            if(d->stream[1]<0){
                if((p->codec_id!=AV_CODEC_ID_BINKAUDIO_DCT && p->codec_id!=AV_CODEC_ID_BINKAUDIO_RDFT) || p->sample_rate<8000 || p->sample_rate>192000 || p->ch_layout.nb_channels<1 || p->ch_layout.nb_channels>2)goto bad;
                d->stream[1]=(int)i;d->info.audio_rate=p->sample_rate;d->info.audio_channels=p->ch_layout.nb_channels;
            }
        }else goto bad;
    }
    if(d->stream[0]<0)goto bad;
    for(unsigned i=0;i<2;i++)if(d->stream[i]>=0){
        AVCodecParameters *p=d->format->streams[d->stream[i]]->codecpar;const AVCodec *codec=avcodec_find_decoder(p->codec_id);if(!codec)goto bad;
        d->codec[i]=avcodec_alloc_context3(codec);if(!d->codec[i] || avcodec_parameters_to_context(d->codec[i],p)<0)goto bad;
        d->codec[i]->max_pixels=1920*1080;d->codec[i]->thread_count=1;if(avcodec_open2(d->codec[i],codec,NULL)<0)goto bad;
    }
    if(d->codec[1]){
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if(swr_alloc_set_opts2(&d->resample,&stereo,AV_SAMPLE_FMT_S16,48000,&d->codec[1]->ch_layout,d->codec[1]->sample_fmt,d->codec[1]->sample_rate,0,NULL)<0 || swr_init(d->resample)<0)goto bad;
    }
    d->frame=av_frame_alloc();d->packet=av_packet_alloc();d->rgba=av_malloc((size_t)d->info.width*d->info.height*4);
    if(!d->frame || !d->packet || !d->rgba)goto bad;
    *info=d->info;*out=d;return 1;
bad:fnv_bink_decoder_close(d);return 0;
}
static int audio_frame(struct fnv_bink_decoder *d,struct fnv_bink_frame *out,int drain){
    int rate=d->codec[1]->sample_rate,n=drain?0:d->frame->nb_samples;
    if(n<0 || n>262144)return -1;
    int64_t capacity=av_rescale_rnd(swr_get_delay(d->resample,rate)+n,48000,rate,AV_ROUND_UP);
    if(capacity<0 || capacity>262144)return -1;
    if(!capacity)return 0;
    if((uint32_t)capacity>d->pcm_capacity){
        void *p=av_realloc(d->pcm,(size_t)capacity*4);if(!p)return -1;
        d->pcm=p;d->pcm_capacity=(uint32_t)capacity;
    }
    unsigned char *output=(unsigned char*)d->pcm;
    int got=swr_convert(d->resample,&output,(int)capacity,drain?NULL:(const unsigned char **)d->frame->extended_data,n);if(got<0)return -1;
    out->pcm=d->pcm;out->audio_frames=(uint32_t)got;out->pts_us=av_rescale((int64_t)d->audio_position,1000000,48000);d->audio_position+=(uint32_t)got;return got!=0;
}
static enum fnv_bink_decode_result decode_next(struct fnv_bink_decoder *d,struct fnv_bink_frame *out){
    if(!d || !out || d->failed)return FNV_BINK_DECODE_ERROR;
    memset(out,0,sizeof(*out));
    for(;;){
        if(d->current>=0){
            av_frame_unref(d->frame);int r=avcodec_receive_frame(d->codec[d->current],d->frame);
            if(r==AVERROR_EOF || r==AVERROR(EAGAIN)){d->current=-1;continue;}
            if(r<0)goto bad;
            if(d->current==1){r=audio_frame(d,out,0);if(r<0)goto bad;if(r)return FNV_BINK_AUDIO;continue;}
            if(d->frame->width!=(int)d->info.width || d->frame->height!=(int)d->info.height || d->frame->pts==AV_NOPTS_VALUE || d->frame->pts<0)goto bad;
            d->scale=sws_getCachedContext(d->scale,d->frame->width,d->frame->height,(enum AVPixelFormat)d->frame->format,d->frame->width,d->frame->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);if(!d->scale)goto bad;
            unsigned char *planes[4]={d->rgba,NULL,NULL,NULL};int pitch[4]={(int)d->info.width*4,0,0,0};
            uint64_t start=0;(void)fnv_host_now_ns(&start);
            int scaled=sws_scale(d->scale,(const unsigned char *const*)d->frame->data,d->frame->linesize,0,d->frame->height,planes,pitch);
            metric_elapsed(&convert_time,start);if(scaled!=d->frame->height)goto bad;
            out->rgba=d->rgba;out->width=d->info.width;out->height=d->info.height;out->pitch=d->info.width*4;out->pts_us=av_rescale_q(d->frame->pts,d->video_timebase,(AVRational){1,1000000});return FNV_BINK_VIDEO;
        }
        if(d->eof){
            for(unsigned i=0;i<2;i++)if(d->codec[i] && !d->flushed[i]){d->flushed[i]=1;if(avcodec_send_packet(d->codec[i],NULL)<0)goto bad;d->current=(int)i;break;}
            if(d->current>=0)continue;
            if(d->resample){int r=audio_frame(d,out,1);if(r<0)goto bad;if(r)return FNV_BINK_AUDIO;}
            return FNV_BINK_END;
        }
        int r=av_read_frame(d->format,d->packet);
        if(r==AVERROR_EOF){d->eof=1;continue;}
        if(r<0)goto bad;
        int kind=d->packet->stream_index==d->stream[0]?0:d->packet->stream_index==d->stream[1]?1:-1;
        if(kind>=0){r=avcodec_send_packet(d->codec[kind],d->packet);av_packet_unref(d->packet);if(r<0)goto bad;d->current=kind;}
        else av_packet_unref(d->packet);
    }
bad:d->failed=1;memset(out,0,sizeof(*out));return FNV_BINK_DECODE_ERROR;
}
enum fnv_bink_decode_result fnv_bink_decoder_next(struct fnv_bink_decoder *d,struct fnv_bink_frame *out){
    uint64_t start=0;(void)fnv_host_now_ns(&start);enum fnv_bink_decode_result result=decode_next(d,out);metric_elapsed(&next_time,start);
    if(result==FNV_BINK_VIDEO)atomic_fetch_add_explicit(&video_count,1,memory_order_relaxed);
    if(result==FNV_BINK_AUDIO)atomic_fetch_add_explicit(&audio_count,out->audio_frames,memory_order_relaxed);
    return result;
}
