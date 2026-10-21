#ifndef FNV_HOST_FILE_H
#define FNV_HOST_FILE_H
/* Original stdio bridge. Only existing regular files are opened, in rb mode. */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <share.h>
#include <sys/stat.h>
static inline int fnv_host_file_seek(FILE *f,int64_t at){return _fseeki64(f,at,SEEK_SET)==0;}
static inline int64_t fnv_host_file_tell(FILE *f){return _ftelli64(f);}
static inline int fnv_host_file_size(FILE *f,uint64_t *size){
    struct _stat64 s;if(_fstat64(_fileno(f),&s) || !(s.st_mode&_S_IFREG) || s.st_size<0)return 0;
    *size=(uint64_t)s.st_size;return 1;
}
static inline int fnv_host_file_directory(const char *path){struct _stat64 s;return !_stat64(path,&s) && (s.st_mode&_S_IFDIR);}
static inline int fnv_host_file_exists(const char *path){struct _stat64 s;return !_stat64(path,&s);}
static inline int fnv_host_file_mkdir(const char *path){if(!_mkdir(path))return 1;return errno==EEXIST && fnv_host_file_directory(path);}
/* disposition: 0=read existing, 1=create new, 2=create/truncate,
 * 3=open existing, 4=open or create, 5=truncate existing. */
static inline FILE *fnv_host_file_open(const char *path,int disposition){
    int flags=_O_BINARY|_O_RDWR,mode=_S_IREAD|_S_IWRITE;
    if(disposition==0)flags=_O_BINARY|_O_RDONLY;
    else if(disposition==1)flags|=_O_CREAT|_O_EXCL;
    else if(disposition==2)flags|=_O_CREAT|_O_TRUNC;
    else if(disposition==4)flags|=_O_CREAT;
    else if(disposition==5)flags|=_O_TRUNC;
    int fd=_open(path,flags,mode);if(fd<0)return NULL;
    FILE *stream=_fdopen(fd,disposition==0?"rb":"r+b");if(!stream)_close(fd);return stream;
}
static inline int fnv_host_file_sync(FILE *stream){return fflush(stream)==0 && _commit(_fileno(stream))==0;}
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
static inline int fnv_host_file_exists(const char *path){struct stat s;return !stat(path,&s);}
static inline int fnv_host_file_mkdir(const char *path){if(!mkdir(path,0700))return 1;return errno==EEXIST && fnv_host_file_directory(path);}
/* disposition: 0=read existing, 1=create new, 2=create/truncate,
 * 3=open existing, 4=open or create, 5=truncate existing. */
static inline FILE *fnv_host_file_open(const char *path,int disposition){
    int flags=O_RDWR,mode=0600;
    if(disposition==0)flags=O_RDONLY;
    else if(disposition==1)flags|=O_CREAT|O_EXCL;
    else if(disposition==2)flags|=O_CREAT|O_TRUNC;
    else if(disposition==4)flags|=O_CREAT;
    else if(disposition==5)flags|=O_TRUNC;
#ifdef O_NOFOLLOW
    if(disposition)flags|=O_NOFOLLOW;
#endif
    int fd=open(path,flags,mode);if(fd<0)return NULL;
    FILE *stream=fdopen(fd,disposition==0?"rb":"r+b");if(!stream)close(fd);return stream;
}
static inline int fnv_host_file_sync(FILE *stream){return fflush(stream)==0 && fsync(fileno(stream))==0;}
#endif
static inline uint32_t fnv_host_file_error(int value){
    switch(value){case ENOENT:return 2;case ENOTDIR:return 3;case EACCES:return 5;case EEXIST:return 80;case EMFILE:case ENFILE:return 4;
        case ENOMEM:return 8;case EINVAL:return 87;default:return 1117;}
}
#endif

