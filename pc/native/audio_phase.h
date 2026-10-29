#ifndef FNV_AUDIO_PHASE_H
#define FNV_AUDIO_PHASE_H
#include <stdint.h>
/* Same cursor evolution as advancing one output frame at a time. Valid mixer
 * buffers have a positive end, phase<end, and frequency<=200000 Hz. */
static inline uint64_t fnv_audio_silent_advance(uint64_t phase,uint32_t frames,
        uint32_t frequency,uint64_t end,uint32_t looping,uint32_t *playing){
    uint64_t next=phase+(uint64_t)frames*frequency;
    if(next<end)return next;
    if(looping)return next%end;
    *playing=0;return 0;
}
#endif
