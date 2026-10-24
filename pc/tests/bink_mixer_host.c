/* Read-only actual media delivery through the actual host DirectSound mixer. */
#include <windows.h>
#include "bink_stream.h"
#include "bink_copy.h"
#include "dsound8.h"
#include "host_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x400000u
struct input {FILE *file;uint64_t size,reads,bytes;int fail;};
static int read_input(void *ctx,unsigned char *p,int bytes){
    struct input *i=ctx;if(i->fail)return -1;
    size_t n=fread(p,1,(size_t)bytes,i->file);i->reads++;i->bytes+=n;return ferror(i->file)?-1:(int)n;
}
static int64_t seek_input(void *ctx,int64_t at,int whence){
    struct input *i=ctx;if(whence==0x10000)return (int64_t)i->size;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?fnv_host_file_tell(i->file):whence==SEEK_END?(int64_t)i->size:-1;
    if(base<0 || (at>0&&base>INT64_MAX-at) || (at<0&&at< -base))return -1;
    at+=base;if(at<0 || (uint64_t)at>i->size || !fnv_host_file_seek(i->file,at))return -1;return at;
}
static uint64_t hash(uint64_t h,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ull;return h;}
static unsigned pump(struct fnv_dsound8 *sound,struct fnv_pcm_queue *queue,uint64_t *h){
    struct fnv_dsound_movie_stats m;assert(fnv_dsound8_movie_stats(sound,&m)==FNV_PCM_OK);
    unsigned n=m.queued<512?m.queued:512;if(!n)return 0;
    int16_t samples[1024];assert(fnv_dsound8_pump(sound,n)==FNV_PCM_OK);assert(fnv_pcm_read(queue,samples,n)==n);
    *h=hash(*h,(const unsigned char*)samples,n*4);return n;
}
int main(int argc,char **argv){
    assert(argc==2);const char *video=getenv("FNV_TEST_BINK_VIDEO"),*limit=getenv("FNV_TEST_BINK_FRAMES");assert(video&&limit);unsigned maximum=(unsigned)strtoul(limit,NULL,10);assert(maximum>0&&maximum<=3000);
    unsigned char *arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=BYTES;cfg.cpu.code_low=BASE+0x1000;cfg.cpu.code_high=BASE+image.size;
    cfg.image_bytes=image.size;cfg.process_id=11;cfg.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK);
    struct fnv_pcm_queue *queue;assert(fnv_pcm_create(1024,&queue)==FNV_PCM_OK);
    struct fnv_dsound8_config dc={0};dc.arena_address=BASE+0x100000;dc.arena_bytes=0x10000;dc.initial_focus=1;dc.output=queue;
    struct fnv_dsound8 *sound;assert(fnv_dsound8_create(process,&dc,&sound)==FNV_CPU_OK);
    struct input input={0};input.file=fopen(video,"rb");assert(input.file&&fnv_host_file_size(input.file,&input.size)&&input.size<=INT64_MAX);
    struct fnv_bink_io io={&input,read_input,seek_input};struct fnv_bink_info info;struct fnv_bink_stream *stream;
    assert(fnv_bink_stream_open(&io,sound,&stream,&info));
    struct fnv_bink_stream *duplicate=(void*)1;uint64_t reads=input.reads;assert(!fnv_bink_stream_open(&io,sound,&duplicate,&info)&&!duplicate&&reads==input.reads);
    assert(fnv_bink_stream_pause(stream,1));struct fnv_bink_frame frame;
    assert(fnv_bink_stream_step(stream,&frame)==FNV_BINK_STREAM_WAIT&&!frame.rgba&&!frame.pcm&&reads==input.reads);
    assert(fnv_bink_stream_pause(stream,0));
    uint64_t audio=0,ahash=14695981039346656037ull,vhash=ahash;unsigned videos=0,waits=0,steps=0;int pressure=0,ended=0;
    size_t packed_bytes=(size_t)(info.width*4+16)*info.height;unsigned char *packed=malloc(packed_bytes);assert(packed);
    while(videos<maximum){
        assert(++steps<100000);enum fnv_bink_stream_result r=fnv_bink_stream_step(stream,&frame);
        if(r==FNV_BINK_STREAM_VIDEO){
            vhash=hash(vhash,frame.rgba,(size_t)frame.pitch*frame.height);videos++;
            assert(fnv_bink_copy_rect(&frame,(size_t)frame.pitch*frame.height,packed,packed_bytes,(int32_t)(info.width*4+16),info.height,0,0,0,0,info.width,info.height,3)==FNV_BINK_COPY_OK);
        }else if(r==FNV_BINK_STREAM_WAIT){
            struct fnv_bink_stream_stats a,b;assert(fnv_bink_stream_stats(stream,&a));reads=input.reads;
            assert(fnv_bink_stream_step(stream,&frame)==FNV_BINK_STREAM_WAIT&&reads==input.reads&&fnv_bink_stream_stats(stream,&b));
            assert(a.pending_audio==b.pending_audio&&a.submitted_audio==b.submitted_audio);waits++;
            if(!pressure){
                int16_t zero[2048]={0};assert(fnv_pcm_write(queue,zero,1024)==FNV_PCM_OK);
                struct fnv_dsound_movie_stats before,after;assert(fnv_dsound8_movie_stats(sound,&before)==FNV_PCM_OK);
                assert(fnv_dsound8_pump(sound,512)==FNV_PCM_FULL&&fnv_dsound8_movie_stats(sound,&after)==FNV_PCM_OK);
                assert(before.queued==after.queued&&before.played==after.played);assert(fnv_pcm_read(queue,zero,1024)==1024);
                assert(fnv_bink_stream_pause(stream,1));assert(fnv_dsound8_pump(sound,64)==FNV_PCM_OK);assert(fnv_pcm_read(queue,zero,64)==64);
                for(unsigned i=0;i<128;i++)assert(!zero[i]);assert(fnv_bink_stream_step(stream,&frame)==FNV_BINK_STREAM_WAIT);
                assert(fnv_bink_stream_pause(stream,0));pressure=1;
            }
            unsigned n=pump(sound,queue,&ahash);assert(n);audio+=n;
        }else if(r==FNV_BINK_STREAM_END){ended=1;assert(fnv_bink_stream_step(stream,&frame)==FNV_BINK_STREAM_END);break;}
        else assert(r==FNV_BINK_STREAM_AUDIO);
    }
    unsigned n;while((n=pump(sound,queue,&ahash))!=0)audio+=n;
    struct fnv_bink_stream_stats final;assert(fnv_bink_stream_stats(stream,&final)&&!final.pending_audio&&final.submitted_audio==audio);
    assert(fnv_bink_stream_close(stream));free(packed);assert(!fclose(input.file));
    /* A failed open cannot clear PCM belonging to the host's existing input. */
    int16_t sentinel[4]={123,234,345,456};assert(fnv_dsound8_movie_write(sound,sentinel,2)==FNV_PCM_OK);
    input.file=fopen(video,"rb");assert(input.file);input.fail=1;
    assert(!fnv_bink_stream_open(&io,sound,&stream,&info)&&!stream);struct fnv_dsound_movie_stats movie;
    assert(fnv_dsound8_movie_stats(sound,&movie)==FNV_PCM_OK&&movie.queued==2);assert(!fclose(input.file));assert(fnv_dsound8_movie_reset(sound)==FNV_PCM_OK);
    /* A later I/O failure is persistent, and close releases the singleton. */
    int late_fault_checked=input.size>65536;
    if(late_fault_checked){input.file=fopen(video,"rb");assert(input.file);input.fail=0;
    assert(fnv_bink_stream_open(&io,sound,&stream,&info));input.fail=1;
    int failed=0;for(unsigned i=0;i<10000;i++){
        enum fnv_bink_stream_result r=fnv_bink_stream_step(stream,&frame);
        if(r==FNV_BINK_STREAM_ERROR){failed=1;assert(fnv_bink_stream_step(stream,&frame)==FNV_BINK_STREAM_ERROR);break;}
        if(r==FNV_BINK_STREAM_WAIT){uint64_t ignored=0;pump(sound,queue,&ignored);}
        assert(r!=FNV_BINK_STREAM_END);
    }assert(failed&&fnv_bink_stream_close(stream));assert(!fclose(input.file));}
    assert(fnv_dsound8_destroy(sound)==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK);fnv_pcm_destroy(queue);assert(VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"video_frames\":%u,\"pcm_frames\":%" PRIu64 ",\"rgba_hash\":\"%016" PRIx64 "\",\"pcm_hash\":\"%016" PRIx64 "\",\"backpressure_waits\":%u,\"decoded_eof\":%s,\"output_full_rollback\":%s,\"late_io_fault_checked\":%s,\"game_started\":false,\"played_on_device\":false}\n",videos,audio,vhash,ahash,waits,ended?"true":"false",pressure?"true":"false",late_fault_checked?"true":"false");return 0;
}
