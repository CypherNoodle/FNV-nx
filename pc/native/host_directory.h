#ifndef FNV_HOST_DIRECTORY_H
#define FNV_HOST_DIRECTORY_H
/* Native metadata only; directory patterns are matched by our guest provider. */
#include "host_file.h"
#include <stdlib.h>
#include <string.h>
struct fnv_host_entry {char name[260],alternate[14];uint32_t attributes,reserved[2];uint64_t creation,access,write,size;};
#ifdef _WIN32
#include <windows.h>
struct fnv_host_directory {HANDLE handle;WIN32_FIND_DATAA data;int ready;};
static inline uint64_t fnv_host_filetime(FILETIME t){return ((uint64_t)t.dwHighDateTime<<32)|t.dwLowDateTime;}
static inline uint32_t fnv_host_metadata(const char *path,struct fnv_host_entry *e){
    WIN32_FILE_ATTRIBUTE_DATA d;if(!GetFileAttributesExA(path,GetFileExInfoStandard,&d))return GetLastError();
    memset(e,0,sizeof(*e));e->attributes=d.dwFileAttributes;e->creation=fnv_host_filetime(d.ftCreationTime);
    e->access=fnv_host_filetime(d.ftLastAccessTime);e->write=fnv_host_filetime(d.ftLastWriteTime);e->size=((uint64_t)d.nFileSizeHigh<<32)|d.nFileSizeLow;return 0;
}
static inline struct fnv_host_directory *fnv_host_directory_open(const char *path,uint32_t *error){
    char pattern[2048];size_t n=strlen(path);if(n+3>sizeof(pattern)){*error=206;return NULL;}memcpy(pattern,path,n);memcpy(pattern+n,"/*",3);
    struct fnv_host_directory *d=malloc(sizeof(*d));if(!d){*error=8;return NULL;}
    d->handle=FindFirstFileA(pattern,&d->data);if(d->handle==INVALID_HANDLE_VALUE){*error=GetLastError();free(d);return NULL;}
    d->ready=1;*error=0;return d;
}
static inline int fnv_host_directory_next(struct fnv_host_directory *d,struct fnv_host_entry *e,uint32_t *error){
    if(!d->ready && !FindNextFileA(d->handle,&d->data)){DWORD code=GetLastError();*error=code==18?0:code;return 0;}d->ready=0;
    memset(e,0,sizeof(*e));WIN32_FIND_DATAA *s=&d->data;memcpy(e->name,s->cFileName,sizeof(e->name));memcpy(e->alternate,s->cAlternateFileName,sizeof(e->alternate));
    e->attributes=s->dwFileAttributes;e->reserved[0]=s->dwReserved0;e->reserved[1]=s->dwReserved1;
    e->creation=fnv_host_filetime(s->ftCreationTime);e->access=fnv_host_filetime(s->ftLastAccessTime);e->write=fnv_host_filetime(s->ftLastWriteTime);
    e->size=((uint64_t)s->nFileSizeHigh<<32)|s->nFileSizeLow;*error=0;return 1;
}
static inline void fnv_host_directory_close(struct fnv_host_directory *d){FindClose(d->handle);free(d);}
#else
#include <dirent.h>
struct fnv_host_directory {DIR *handle;char path[1538];};
static inline uint64_t fnv_host_unix_filetime(int64_t seconds){
    const int64_t epoch=INT64_C(11644473600);
    if(seconds<-epoch || seconds>INT64_MAX-epoch)return 0;
    uint64_t shifted=(uint64_t)(seconds+epoch);return shifted>UINT64_MAX/10000000?0:shifted*10000000;
}
static inline uint32_t fnv_host_metadata(const char *path,struct fnv_host_entry *e){
    struct stat s;if(stat(path,&s))return fnv_host_file_error(errno);
    if(!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode))return 50;
    memset(e,0,sizeof(*e));e->attributes=S_ISDIR(s.st_mode)?16:32;
    if(!(s.st_mode&(S_IWUSR|S_IWGRP|S_IWOTH)))e->attributes|=1;
    e->size=S_ISREG(s.st_mode) && s.st_size>=0?(uint64_t)s.st_size:0;
    e->access=fnv_host_unix_filetime((int64_t)s.st_atime);e->write=fnv_host_unix_filetime((int64_t)s.st_mtime);
    /* POSIX change time is not creation time. Unknown creation remains zero. */
    return 0;
}
static inline struct fnv_host_directory *fnv_host_directory_open(const char *path,uint32_t *error){
    if(strlen(path)>=1538){*error=206;return NULL;}
    DIR *handle=opendir(path);if(!handle){*error=fnv_host_file_error(errno);if(*error==2)*error=3;return NULL;}
    struct fnv_host_directory *d=malloc(sizeof(*d));if(!d){closedir(handle);*error=8;return NULL;}
    d->handle=handle;strcpy(d->path,path);*error=0;return d;
}
static inline int fnv_host_directory_next(struct fnv_host_directory *d,struct fnv_host_entry *e,uint32_t *error){
    for(;;){errno=0;struct dirent *item=readdir(d->handle);if(!item){*error=errno?fnv_host_file_error(errno):0;return 0;}
        size_t n=strlen(item->d_name);if(n>=sizeof(e->name)){*error=206;return 0;}char path[2048];size_t parent=strlen(d->path);
        memcpy(path,d->path,parent);path[parent++]='/';memcpy(path+parent,item->d_name,n+1);
        *error=fnv_host_metadata(path,e);if(*error==2)continue;if(*error)return 0;memcpy(e->name,item->d_name,n+1);return 1;
    }
}
static inline void fnv_host_directory_close(struct fnv_host_directory *d){closedir(d->handle);free(d);}
#endif
#endif
