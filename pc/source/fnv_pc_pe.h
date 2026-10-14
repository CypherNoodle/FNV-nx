#ifndef FNV_PC_PE_H
#define FNV_PC_PE_H
#include <stdio.h>
#define FNV_PC_MAX_IMPORTS 128
struct fnv_pc_pe {
    unsigned image_base, image_bytes, entry_rva;
    unsigned file_characteristics;
    int entry_in_bind, steam_api, galaxy_wrapper;
    unsigned import_count;
    char imports[FNV_PC_MAX_IMPORTS][128];
};
/* Read file headers and import DLL names only. Never map or run guest code. */
int fnv_pc_read_pe(const char *path, struct fnv_pc_pe *info, FILE *log);
#endif
