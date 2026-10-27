#include "engine_movie_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char **argv) {
    assert(argc==2);struct fnv_bink_io io={0};uint64_t size=0;
    assert(fnv_engine_movie_open(argv[1],"Data\\Video\\probe.bik",&io,&size));
    assert(size==8 && io.seek(io.context,0,0x10000)==8);
    unsigned char bytes[8];assert(io.read(io.context,bytes,4)==4 && !memcmp(bytes,"ABCD",4));
    assert(io.seek(io.context,-2,SEEK_CUR)==2);
    assert(io.read(io.context,bytes,4)==4 && !memcmp(bytes,"CDEF",4));
    assert(io.seek(io.context,-1,SEEK_END)==7 && io.read(io.context,bytes,8)==1 && bytes[0]=='H');
    assert(io.read(io.context,bytes,8)==0);
    assert(io.seek(io.context,INT64_MIN,SEEK_CUR)==-1);
    assert(io.seek(io.context,INT64_MAX,SEEK_END)==-1);
    assert(io.seek(io.context,9,SEEK_SET)==-1);
    assert(io.seek(io.context,0,12345)==-1);
    assert(io.read(io.context,bytes,-1)==-1);
    fnv_engine_movie_close(argv[1],&io);assert(!io.context && !io.read && !io.seek);
    const char *invalid[]={"C:\\Data\\Video\\probe.bik","/Data/Video/probe.bik","Data/Video/../probe.bik",
        "Data/Video/./probe.bik","Data//Video/probe.bik","Data/Video/","Data/Video/probe.bik/",
        "Data/Video/probe\x7f.bik","Data/Video/\xc3\xa9.bik","Other/Video/probe.bik","Data/Video/missing.bik"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        struct fnv_bink_io sentinel;memset(&sentinel,0xa5,sizeof(sentinel));io=sentinel;size=123;
        assert(!fnv_engine_movie_open(argv[1],invalid[i],&io,&size));
        assert(size==123 && !memcmp(&io,&sentinel,sizeof(io)));
    }
    puts("PASS sandbox movie I/O: real read/seek/EOF, traversal and overflow rejection, unchanged failure outputs");
    return 0;
}
