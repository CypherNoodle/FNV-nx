#ifndef FNV_ENGINE_CRASH_H
#define FNV_ENGINE_CRASH_H
#include <stdint.h>
/* Initialize the dedicated raw-FS sink before starting guest threads.
 * The crash path does not use stdio, allocation or provider mutexes. */
uint32_t fnv_engine_crash_prepare(const char *revision);
void fnv_engine_crash_guest(const unsigned char *,uint32_t base,uint32_t bytes);
void fnv_engine_crash_phase(uint32_t);
#endif
