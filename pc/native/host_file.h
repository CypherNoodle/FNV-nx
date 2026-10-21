#ifndef FNV_HOST_FILE_H
#define FNV_HOST_FILE_H
/* Original stdio bridge. Only existing regular files are opened, in rb mode. */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
static inline int fnv_host_file_seek(FILE *f,int64_t at){return _fseeki64(f,at,SEEK_SET)==0;}
static inline int64_t fnv_host_file_tell(FILE *f){return _ftelli64(f);}
static inline int fnv_host_file_size(FILE *f,uint64_t *size){
    struct _stat64 s;if(_fstat64(_fileno(f),&s) || !(s.st_mode&_S_IFREG) || s.st_size<0)return 0;
    *size=(uint64_t)s.st_size;return 1;
}
static inline int fnv_host_file_directory(const char *path){struct _stat64 s;return !_stat64(path,&s) && (s.st_mode&_S_IFDIR);}
static inline int fnv_host_file_mkdir(const char *path){if(!_mkdir(path))return 1;return errno==EEXIST && fnv_host_file_directory(path);}
#else
#include <unistd.h>
_Static_assert(sizeof(off_t)>=8,"File backend requires 64-bit off_t");
static inline int fnv_host_file_seek(FILE *f,int64_t at){return fseeko(f,(off_t)at,SEEK_SET)==0;}
static inline int64_t fnv_host_file_tell(FILE *f){return (int64_t)ftello(f);}
static inline int fnv_host_file_size(FILE *f,uint64_t *size){
    struct stat s;if(fstat(fileno(f),&s) || !S_ISREG(s.st_mode) || s.st_size<0)return 0;
    *size=(uint64_t)s.st_size;return 1;
}
static inline int fnv_host_file_directory(const char *path){struct stat s;return !stat(path,&s) && S_ISDIR(s.st_mode);}
static inline int fnv_host_file_mkdir(const char *path){if(!mkdir(path,0700))return 1;return errno==EEXIST && fnv_host_file_directory(path);}
#endif
static inline uint32_t fnv_host_file_error(int value){
    switch(value){case ENOENT:return 2;case ENOTDIR:return 3;case EACCES:return 5;case EMFILE:case ENFILE:return 4;
        case ENOMEM:return 8;case EINVAL:return 87;default:return 1117;}
}
#endif

