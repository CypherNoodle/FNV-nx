#include "mp3_stream.h"
#include "pcm_convert.h"
#include "host_file.h"
#include <stdlib.h>
#include <string.h>
#define MINIMP3_NO_STDIO
#define MINIMP3_SCAN_LIMIT (256*1024)
#define MINIMP3_IMPLEMENTATION
#include <minimp3_ex.h>
struct fnv_mp3 {mp3dec_ex_t decoder;mp3dec_io_t adapter;struct fnv_mp3_io io;FILE *file;struct fnv_pcm_converter convert;
    int16_t raw[2048],pending[1024];uint32_t raw_at,raw_frames,pending_frames;int io_failed,opened;enum fnv_mp3_result pending_result;
    struct fnv_mp3_info info;};
static size_t read_cb(void *buffer,size_t size,void *context){
    struct fnv_mp3 *s=context;size_t n=0;
    if(!s->io.read(s->io.context,buffer,size,&n) || n>size){s->io_failed=1;return 0;}return n;
}
static int seek_cb(uint64_t position,void *context){
    struct fnv_mp3 *s=context;if(!s->io.seek(s->io.context,position)){s->io_failed=1;return -1;}return 0;
}
static enum fnv_mp3_result setup(struct fnv_mp3 *s){
    s->io_failed=0;s->raw_at=s->raw_frames=s->pending_frames=0;memset(&s->info,0,sizeof(s->info));
    int r=mp3dec_ex_open_cb(&s->decoder,&s->adapter,MP3D_DO_NOT_SCAN);
    if(r || s->io_failed || s->decoder.info.channels<1 || s->decoder.info.channels>2 || s->decoder.info.layer!=3 ||
       fnv_pcm_convert_init(&s->convert,(uint32_t)s->decoder.info.hz)!=FNV_CONVERT_OK){
        mp3dec_ex_close(&s->decoder);s->opened=0;
        return s->io_failed?FNV_MP3_IO:r==MP3D_E_MEMORY?FNV_MP3_NO_MEMORY:FNV_MP3_FORMAT;
    }
    s->opened=1;s->info.source_rate=(uint32_t)s->decoder.info.hz;s->info.source_channels=(uint32_t)s->decoder.info.channels;return FNV_MP3_OK;
}
enum fnv_mp3_result fnv_mp3_open(const struct fnv_mp3_io *io,struct fnv_mp3 **out){
    if(!out)return FNV_MP3_INVALID;
    *out=NULL;if(!io || !io->read || !io->seek)return FNV_MP3_INVALID;
    struct fnv_mp3 *s=calloc(1,sizeof(*s));if(!s)return FNV_MP3_NO_MEMORY;
    s->io=*io;s->adapter=(mp3dec_io_t){read_cb,s,seek_cb,s};enum fnv_mp3_result r=setup(s);
    if(r!=FNV_MP3_OK){free(s);return r;}*out=s;return FNV_MP3_OK;
}
static int file_read(void *context,void *buffer,size_t capacity,size_t *read){
    FILE *f=context;*read=fread(buffer,1,capacity,f);return !ferror(f);
}
static int file_seek(void *context,uint64_t offset){return offset<=INT64_MAX && fnv_host_file_seek(context,(int64_t)offset);}
enum fnv_mp3_result fnv_mp3_open_file(const char *path,struct fnv_mp3 **out){
    if(!out)return FNV_MP3_INVALID;
    *out=NULL;if(!path)return FNV_MP3_INVALID;
    FILE *f=fopen(path,"rb");if(!f)return FNV_MP3_IO;uint64_t bytes;
    if(!fnv_host_file_size(f,&bytes) || !bytes){fclose(f);return FNV_MP3_FORMAT;}
    struct fnv_mp3_io io={f,file_read,file_seek};enum fnv_mp3_result r=fnv_mp3_open(&io,out);
    if(r!=FNV_MP3_OK)fclose(f);else (*out)->file=f;return r;
}
void fnv_mp3_close(struct fnv_mp3 *s){if(s){if(s->opened)mp3dec_ex_close(&s->decoder);if(s->file)fclose(s->file);free(s);}}
enum fnv_mp3_result fnv_mp3_info(struct fnv_mp3 *s,struct fnv_mp3_info *out){if(!s || !out)return FNV_MP3_INVALID;*out=s->info;return FNV_MP3_OK;}
static int source(void *context,int16_t out[2]){
    struct fnv_mp3 *s=context;
    if(s->raw_at==s->raw_frames){
        size_t n=mp3dec_ex_read(&s->decoder,s->raw,1024*s->info.source_channels);
        if(s->io_failed || s->decoder.last_error || n%s->info.source_channels)return -1;
        s->raw_at=0;s->raw_frames=(uint32_t)(n/s->info.source_channels);if(!n)return 0;
    }
    out[0]=s->raw[s->raw_at*s->info.source_channels];out[1]=s->info.source_channels==1?out[0]:s->raw[s->raw_at*2+1];
    s->raw_at++;s->info.source_frames++;return 1;
}
static enum fnv_mp3_result decode(struct fnv_mp3 *s,int16_t *out,uint32_t capacity,uint32_t *frames){
    enum fnv_convert_result r=fnv_pcm_convert_read(&s->convert,source,s,out,capacity,frames);s->info.output_frames+=*frames;
    return r==FNV_CONVERT_OK?FNV_MP3_OK:r==FNV_CONVERT_EOF?FNV_MP3_EOF:r==FNV_CONVERT_SOURCE_ERROR?(s->io_failed?FNV_MP3_IO:FNV_MP3_FORMAT):FNV_MP3_INVALID;
}
enum fnv_mp3_result fnv_mp3_read(struct fnv_mp3 *s,int16_t *out,uint32_t capacity,uint32_t *frames){
    if(!frames)return FNV_MP3_INVALID;
    *frames=0;if(!s || !s->opened)return FNV_MP3_INVALID;if(s->pending_frames)return FNV_MP3_BUSY;
    return decode(s,out,capacity,frames);
}
enum fnv_mp3_result fnv_mp3_pump(struct fnv_mp3 *s,struct fnv_pcm_queue *queue){
    if(!s || !s->opened || !queue)return FNV_MP3_INVALID;
    if(!s->pending_frames){
        struct fnv_pcm_stats stats;if(fnv_pcm_stats(queue,&stats)!=FNV_PCM_OK)return FNV_MP3_INVALID;
        uint32_t capacity=stats.capacity<512?stats.capacity:512;
        s->pending_result=decode(s,s->pending,capacity,&s->pending_frames);
        if(!s->pending_frames)return s->pending_result;
    }
    enum fnv_pcm_result r=fnv_pcm_write(queue,s->pending,s->pending_frames);
    if(r==FNV_PCM_FULL)return FNV_MP3_AGAIN;
    if(r!=FNV_PCM_OK)return FNV_MP3_INVALID;
    s->pending_frames=0;return s->pending_result;
}
enum fnv_mp3_result fnv_mp3_rewind(struct fnv_mp3 *s){
    if(!s || !s->opened)return FNV_MP3_INVALID;
    mp3dec_ex_close(&s->decoder);s->opened=0;return setup(s);
}
