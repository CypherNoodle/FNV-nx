/* Host test of the actual fsdev restriction: rename cannot replace a file. */
#undef rename
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
int fnv_test_rename(const char *source, const char *dest) {
    struct stat st;
    if (!stat(dest,&st)) { errno=EEXIST; return -1; }
    if (getenv("FNV_TEST_FAIL_COMMIT") && strstr(source,".tmp")) { errno=EIO; return -1; }
    return rename(source,dest);
}
