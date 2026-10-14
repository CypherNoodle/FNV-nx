#include "fnv_pc_bootstrap.h"
#include <stdio.h>
int main(int argc, char **argv) {
    if (!fnv_pc_bootstrap(&argc,&argv)) return 2;
    if (argc != 2) return 3;
    printf("buttons=%04x,%04x,%04x,%04x,%04x\n",fnv_pc_map_buttons(0x1000),fnv_pc_map_buttons(0x2000),fnv_pc_map_buttons(0x4000),fnv_pc_map_buttons(0x8000),fnv_pc_map_buttons(0x000f));
    puts(argv[1]);
    return 0;
}
