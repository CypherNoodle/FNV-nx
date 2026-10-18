#define _POSIX_C_SOURCE 200809L
#include "host_directory.h"
#include "host_wallclock.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
int main(int argc,char **argv){
    assert(argc==2);char path[2048];
    const char *names[]={"first","second"};
    for(unsigned i=0;i<2;++i){snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);FILE *file=fopen(path,"wb");assert(file && fclose(file)==0);}
    snprintf(path,sizeof(path),"%s/fifo",argv[1]);assert(mkfifo(path,0600)==0);
    struct fnv_host_entry entry;assert(fnv_host_metadata(path,&entry)==50);
    uint32_t error=999;struct fnv_host_directory *d=fnv_host_directory_open(argv[1],&error);assert(d && !error);
    unsigned mask=0;while(fnv_host_directory_next(d,&entry,&error)){
        if(!strcmp(entry.name,".") || !strcmp(entry.name,".."))continue;
        if(!strcmp(entry.name,"first"))mask|=1;else if(!strcmp(entry.name,"second"))mask|=2;else assert(0);
    }
    assert(!error && mask==3 && fnv_host_directory_take_skipped(d)==1 && !fnv_host_directory_take_skipped(d));
    assert(!fnv_host_directory_next(d,&entry,&error) && !error);fnv_host_directory_close(d);
    uint64_t utc;assert(fnv_host_utc_filetime(&utc) && utc>UINT64_C(116444736000000000));
    puts("{\"status\":\"PASS\",\"posix_special_entries_skipped\":1,\"regular_entries_after_scan\":2,\"posix_realtime_available\":true,\"switch_runtime_tested\":false}");return 0;
}
