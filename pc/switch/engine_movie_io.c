#include "engine_movie_io.h"
#include "host_file.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
struct movie_file {FILE *file;uint64_t size;};
static int read_movie(void *context,unsigned char *out,int count) {
    struct movie_file *m=context;
    if(!m || !out || count<0)return -1;
    size_t got=fread(out,1,(size_t)count,m->file);
    return ferror(m->file)?-1:(int)got;
}
static int64_t seek_movie(void *context,int64_t at,int origin) {
    struct movie_file *m=context;if(!m)return -1;
    if(origin==0x10000)return (int64_t)m->size;
    int64_t base=origin==SEEK_SET?0:origin==SEEK_CUR?fnv_host_file_tell(m->file):origin==SEEK_END?(int64_t)m->size:-1;
    if(base<0 || (at>0 && base>INT64_MAX-at) || (at<0 && at< -base))return -1;
    at+=base;if((uint64_t)at>m->size || !fnv_host_file_seek(m->file,at))return -1;
    return at;
}
int fnv_engine_movie_open(void *root,const char *name,struct fnv_bink_io *out,uint64_t *size) {
    if(!root || !name || !out || !size)return 0;
    char rel[512],path[1024];size_t length=0;
    for(;length<sizeof(rel);length++) {
        unsigned char c=(unsigned char)name[length];
        if(c>=128 || (c && c<32) || c==':' || c==127)return 0;
        rel[length]=c=='\\'?'/':(char)c;if(!c)break;
    }
    if(length==sizeof(rel) || length<=11)return 0;
    /* ASCII-insensitive prefix, preserving the actual asset path spelling. */
    const char prefix[]="data/video/";
    for(unsigned i=0;i<11;i++){unsigned char c=(unsigned char)rel[i];if(c>='A' && c<='Z')c+=32;if(c!=(unsigned char)prefix[i])return 0;}
    for(size_t begin=0;begin<length;) {
        size_t end=begin;while(end<length && rel[end]!='/')end++;
        size_t n=end-begin;
        if(!n || (n==1 && rel[begin]=='.') || (n==2 && rel[begin]=='.' && rel[begin+1]=='.'))return 0;
        begin=end+1;
    }
    if(rel[length-1]=='/')return 0;
    int written=snprintf(path,sizeof(path),"%s/%s",(const char *)root,rel);
    if(written<0 || (size_t)written>=sizeof(path))return 0;
    struct movie_file *m=calloc(1,sizeof(*m));if(!m)return 0;
    m->file=fopen(path,"rb");
    if(!m->file || !fnv_host_file_size(m->file,&m->size) || m->size>UINT32_MAX) {
        if(m->file)fclose(m->file);
        free(m);return 0;
    }
    *out=(struct fnv_bink_io){m,read_movie,seek_movie};*size=m->size;return 1;
}
void fnv_engine_movie_close(void *root,struct fnv_bink_io *io) {
    (void)root;if(!io || !io->context)return;
    struct movie_file *m=io->context;fclose(m->file);free(m);memset(io,0,sizeof(*io));
}
