#ifndef FNV_PC_BOOTSTRAP_H
#define FNV_PC_BOOTSTRAP_H
/* Called after Autorun has initialized its console and log. */
int fnv_pc_bootstrap(int *argc, char ***argv);
unsigned short fnv_pc_map_buttons(unsigned short buttons);
void fnv_pc_note_failure(const char *reason);
#endif
