/* Read-only private media probe. Never plays audio or writes decoded assets. */
#include "bink_decoder.h"
#include "bink_copy.h"
#include "host_file.h"
#include "host_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
struct input {FILE *file;uint64_t size,reads,bytes;int mode;};
static int read_bytes(void *context,unsigned char *p,int size){
    struct input *s=context;if(size<1 || s->mode==1 || (s->mode==2 && s->bytes>=65536))return -1;
    int64_t position=fnv_host_file_tell(s->file);if(position<0)return -1;
    size_t n=fread(p,1,(size_t)size,s->file);s->reads++;s->bytes+=n;
    if(s->mode==3)for(unsigned i=0;i<4;i++)if(position<=20+i && (uint64_t)(20+i-position)<n)p[20+i-position]=(unsigned char)(1921u>>(8*i));
    return ferror(s->file)?-1:(int)n;
}
static int64_t seek_bytes(void *context,int64_t at,int whence){
    struct input *s=context;if(whence==0x10000)return (int64_t)s->size;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?fnv_host_file_tell(s->file):whence==SEEK_END?(int64_t)s->size:-1;
    if(base<0 || (at>0 && base>INT64_MAX-at) || (at<0 && at< -base))return -1;
    at+=base;if(at<0 || (uint64_t)at>s->size || !fnv_host_file_seek(s->file,at))return -1;
    return at;
}
static uint64_t hash(uint64_t h,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ull;return h;}
int main(int argc,char **argv){
    if(argc!=3 && argc!=4)return 2;
    char *end;unsigned long maximum=strtoul(argv[2],&end,10);if(*end || maximum<1 || maximum>3000)return 2;
    struct input input={0};if(argc==4){input.mode=atoi(argv[3]);if(input.mode<1 || input.mode>3)return 2;}
    input.file=fopen(argv[1],"rb");if(!input.file)return 3;
    if(!fnv_host_file_size(input.file,&input.size) || input.size>INT64_MAX){fclose(input.file);return 3;}
    struct fnv_bink_io io={&input,read_bytes,seek_bytes};struct fnv_bink_decoder *decoder=NULL;struct fnv_bink_info info;
    if(!fnv_bink_decoder_open(&io,&decoder,&info)){fclose(input.file);puts("{\"status\":\"REJECTED\",\"phase\":\"open\"}");return 4;}
    uint32_t copy_pitch=info.width*4+16;size_t copy_bytes=(size_t)copy_pitch*info.height;
    unsigned char *packed=malloc(copy_bytes);if(!packed){fnv_bink_decoder_close(decoder);fclose(input.file);return 5;}memset(packed,0xa5,copy_bytes);
    unsigned videos=0,audios=0;uint64_t samples=0,vhash=14695981039346656037ull,ahash=vhash,start=0,finish=0;int64_t last_video=-1,last_audio=-1;
    if(!fnv_host_now_ns(&start)){free(packed);fnv_bink_decoder_close(decoder);fclose(input.file);return 5;}
    enum fnv_bink_decode_result result=FNV_BINK_END;
    while(videos<maximum){
        struct fnv_bink_frame frame;result=fnv_bink_decoder_next(decoder,&frame);
        if(result==FNV_BINK_VIDEO){
            if(frame.pts_us<=last_video || !frame.rgba || frame.width!=info.width || frame.height!=info.height || frame.pitch!=info.width*4){result=FNV_BINK_DECODE_ERROR;break;}
            if(fnv_bink_copy_rect(&frame,(size_t)frame.pitch*frame.height,packed,copy_bytes,(int32_t)copy_pitch,info.height,0,0,0,0,info.width,info.height,0x80000003u)!=FNV_BINK_COPY_OK){result=FNV_BINK_DECODE_ERROR;break;}
            int mismatch=0;
            for(unsigned y=0;y<frame.height;y++){
                for(unsigned x=0;x<frame.width;x++){const unsigned char *s=frame.rgba+(size_t)y*frame.pitch+x*4,*d=packed+(size_t)y*copy_pitch+x*4;
                    if(d[0]!=s[2] || d[1]!=s[1] || d[2]!=s[0] || d[3])mismatch=1;}
                for(unsigned x=frame.width*4;x<copy_pitch;x++)if(packed[(size_t)y*copy_pitch+x]!=0xa5)mismatch=1;
            }
            if(mismatch){result=FNV_BINK_DECODE_ERROR;break;}
            last_video=frame.pts_us;vhash=hash(vhash,frame.rgba,(size_t)frame.pitch*frame.height);videos++;
        }else if(result==FNV_BINK_AUDIO){
            if(frame.pts_us<last_audio || !frame.pcm || !frame.audio_frames){result=FNV_BINK_DECODE_ERROR;break;}
            last_audio=frame.pts_us;ahash=hash(ahash,(const unsigned char*)frame.pcm,(size_t)frame.audio_frames*4);audios++;samples+=frame.audio_frames;
        }else break;
    }
    if(!fnv_host_now_ns(&finish))result=FNV_BINK_DECODE_ERROR;
    if(result==FNV_BINK_END){struct fnv_bink_frame frame;if(fnv_bink_decoder_next(decoder,&frame)!=FNV_BINK_END)result=FNV_BINK_DECODE_ERROR;}
    free(packed);fnv_bink_decoder_close(decoder);int closed=fclose(input.file)==0;
    printf("{\"status\":\"%s\",\"width\":%u,\"height\":%u,\"total_frames\":%u,\"rate_num\":%u,\"rate_den\":%u,\"audio_tracks\":%u,\"source_audio_rate\":%u,\"source_audio_channels\":%u,\"video_frames\":%u,\"audio_events\":%u,\"pcm_stereo_48000_frames\":%" PRIu64 ",\"rgba_hash\":\"%016" PRIx64 "\",\"pcm_hash\":\"%016" PRIx64 "\",\"elapsed_ns\":%" PRIu64 ",\"io_reads\":%" PRIu64 ",\"io_bytes\":%" PRIu64 ",\"played\":false,\"game_started\":false,\"switch_hardware_tested\":false}\n",
        result==FNV_BINK_DECODE_ERROR || !closed?"FAILED":"PASS",info.width,info.height,info.frames,info.rate_num,info.rate_den,info.audio_tracks,info.audio_rate,info.audio_channels,videos,audios,samples,vhash,ahash,finish-start,input.reads,input.bytes);
    return result==FNV_BINK_DECODE_ERROR || !closed?6:0;
}
