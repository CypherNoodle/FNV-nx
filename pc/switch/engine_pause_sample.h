#ifndef FNV_ENGINE_PAUSE_SAMPLE_H
#define FNV_ENGINE_PAUSE_SAMPLE_H
#include <stdint.h>
/* All callbacks run on the sampler. Capture must avoid locks/IO owned by the
 * paused target. Resume runs even if capture fails. The caller must handle a
 * failed resume before doing anything else. No implicit thread termination. */
struct fnv_pause_ops {void *context;uint32_t (*pause)(void *),(*capture)(void *),(*resume)(void *);};
struct fnv_pause_result {uint32_t pause,capture,resume;};
static inline struct fnv_pause_result fnv_pause_sample(const struct fnv_pause_ops *ops){
    struct fnv_pause_result r={0,UINT32_MAX,UINT32_MAX};r.pause=ops->pause(ops->context);
    if(!r.pause){r.capture=ops->capture(ops->context);r.resume=ops->resume(ops->context);}return r;
}
#endif
