#include "fnv_pc_pe.h"
#include <stdio.h>
int main(int argc, char **argv) {
    struct fnv_pc_pe pe;
    if (argc!=2 || !fnv_pc_read_pe(argv[1],&pe,stderr)) return 2;
    printf("%u %u %u %d %d %d %u %d\n",pe.image_base,pe.image_bytes,pe.entry_rva,
        pe.entry_in_bind,pe.steam_api,pe.galaxy_wrapper,pe.import_count,!!(pe.file_characteristics & 0x20));
    for (unsigned i=0;i<pe.import_count;++i) puts(pe.imports[i]);
    return 0;
}
