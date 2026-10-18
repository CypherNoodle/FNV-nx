#include "mp3_stream.h"
#include "host_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
struct bad_io {unsigned calls;};
static int bad_read(void *p,void *buffer,size_t size,size_t *n){(void)buffer;(void)size;((struct bad_io *)p)->calls++;*n=0;return 0;}
static int bad_seek(void *p,uint64_t pos){(void)p;(void)pos;return 1;}
static int empty_read(void *p,void *buffer,size_t size,size_t *n){(void)p;(void)buffer;(void)size;*n=0;return 1;}
struct failing_file {FILE *file;int fail;};
static int file_read(void *context,void *buffer,size_t size,size_t *n){struct failing_file *f=context;
    if(f->fail){*n=0;return 0;}*n=fread(buffer,1,size,f->file);return !ferror(f->file);}
static int file_seek(void *context,uint64_t at){struct failing_file *f=context;return at<=INT64_MAX && fnv_host_file_seek(f->file,(int64_t)at);}
int main(int argc,char **argv){
    assert(argc==5);struct fnv_mp3 *s=NULL;struct bad_io bad={0};struct fnv_mp3_io io={&bad,bad_read,bad_seek};
    assert(fnv_mp3_open(&io,&s)==FNV_MP3_IO && !s && bad.calls);
    struct fnv_mp3_io empty={NULL,empty_read,bad_seek};assert(fnv_mp3_open(&empty,&s)==FNV_MP3_FORMAT && !s);
    struct failing_file failing={fopen(argv[1],"rb"),0};assert(failing.file);
    struct fnv_mp3_io faulty={&failing,file_read,file_seek};assert(fnv_mp3_open(&faulty,&s)==FNV_MP3_OK);
    failing.fail=1;int16_t failed_samples[32];uint32_t failed_frames=99;
    assert(fnv_mp3_read(s,failed_samples,16,&failed_frames)==FNV_MP3_IO && !failed_frames);
    fnv_mp3_close(s);assert(fclose(failing.file)==0);
    assert(fnv_mp3_open_file(argv[1],&s)==FNV_MP3_OK);struct fnv_mp3_info first;assert(fnv_mp3_info(s,&first)==FNV_MP3_OK);
    FILE *out=fopen(argv[2],"wb");assert(out);unsigned block=(unsigned)strtoul(argv[3],NULL,10);assert(block && block<=4096);
    int16_t pcm[8192];uint64_t frames=0;int nonzero=0;
    if(!strcmp(argv[4],"pump")){
        struct fnv_pcm_queue *q;assert(fnv_pcm_create(127,&q)==FNV_PCM_OK);enum fnv_mp3_result result=FNV_MP3_OK;
        unsigned iteration=0;
        while(result!=FNV_MP3_EOF){
            result=fnv_mp3_pump(s,q);assert(result==FNV_MP3_OK || result==FNV_MP3_AGAIN || result==FNV_MP3_EOF);
            if(result==FNV_MP3_AGAIN){uint32_t n=999;assert(fnv_mp3_read(s,pcm,1,&n)==FNV_MP3_BUSY && !n);}
            if(iteration++%3 && result!=FNV_MP3_AGAIN && result!=FNV_MP3_EOF)continue;
            struct fnv_pcm_stats stats;assert(fnv_pcm_stats(q,&stats)==FNV_PCM_OK);
            while(stats.queued){unsigned n=stats.queued<block?stats.queued:block;assert(fnv_pcm_read(q,pcm,n)==n);
                assert(fwrite(pcm,4,n,out)==n);frames+=n;for(unsigned k=0;k<n*2;k++)nonzero|=pcm[k]!=0;
                assert(fnv_pcm_stats(q,&stats)==FNV_PCM_OK);}
        }
        struct fnv_pcm_stats stats;assert(fnv_pcm_stats(q,&stats)==FNV_PCM_OK && stats.full_writes>0 && !stats.silence);fnv_pcm_destroy(q);
    }else{
        for(;;){uint32_t n=0;enum fnv_mp3_result result=fnv_mp3_read(s,pcm,block,&n);
            assert(result==FNV_MP3_OK || result==FNV_MP3_EOF);
            if(result==FNV_MP3_EOF){assert(!n);break;}assert(n && fwrite(pcm,4,n,out)==n);frames+=n;
            for(unsigned k=0;k<n*2;k++)nonzero|=pcm[k]!=0;
        }
    }
    assert(fclose(out)==0 && frames && nonzero);struct fnv_mp3_info info;assert(fnv_mp3_info(s,&info)==FNV_MP3_OK);
    assert(frames==info.output_frames && frames==(info.source_frames*48000+info.source_rate-1)/info.source_rate);
    assert(fnv_mp3_rewind(s)==FNV_MP3_OK);uint32_t count=0;assert(fnv_mp3_read(s,pcm,block,&count)==FNV_MP3_OK && count);
    FILE *check=fopen(argv[2],"rb");int16_t expected[8192];assert(check && fread(expected,4,count,check)==count && !memcmp(expected,pcm,count*4));assert(fclose(check)==0);
    fnv_mp3_close(s);printf("{\"status\":\"PASS\",\"rate\":%u,\"channels\":%u,\"source_frames\":%llu,\"output_frames\":%llu}\n",first.source_rate,first.source_channels,(unsigned long long)info.source_frames,(unsigned long long)frames);return 0;
}
