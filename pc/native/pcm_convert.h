#ifndef FNV_PCM_CONVERT_H
#define FNV_PCM_CONVERT_H
#include <stdint.h>
/* Linear streaming upsampler, stereo s16; native source rates 8000..48000.
 * Callback supplies one stereo frame: 1=data, 0=EOF, -1=error. Host callbacks
 * only, no x86 addresses. One owner; no concurrent reads/reset. */
typedef int (*fnv_pcm_source)(void *,int16_t stereo[2]);
enum fnv_convert_result {FNV_CONVERT_OK,FNV_CONVERT_EOF,FNV_CONVERT_INVALID,FNV_CONVERT_SOURCE_ERROR};
struct fnv_pcm_converter {uint32_t rate,phase;int16_t a[2],b[2];int initialized,have,b_real,failed;};
enum fnv_convert_result fnv_pcm_convert_init(struct fnv_pcm_converter *,uint32_t source_rate);
enum fnv_convert_result fnv_pcm_convert_read(struct fnv_pcm_converter *,fnv_pcm_source,void *,int16_t *,uint32_t capacity,uint32_t *frames);
#endif
