#ifndef FNV_ENGINE_MOVIE_IO_H
#define FNV_ENGINE_MOVIE_IO_H
#include "bink_decoder.h"
/* context is an immutable root directory, retained through close. Only relative
 * Data/Video paths, ASCII and ordinary components; files are opened read-only. */
int fnv_engine_movie_open(void *,const char *,struct fnv_bink_io *,uint64_t *);
void fnv_engine_movie_close(void *,struct fnv_bink_io *);
#endif
