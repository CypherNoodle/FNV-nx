#include "pcm_queue.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct fnv_pcm_queue {fnv_host_mutex guard;int16_t *samples;uint32_t head,count,capacity;uint64_t written,read,silence,full_writes;};
enum fnv_pcm_result fnv_pcm_create(uint32_t capacity,struct fnv_pcm_queue **out){
    if(!out)return FNV_PCM_INVALID;
    *out=NULL;
    if(!capacity || capacity>48000)return FNV_PCM_INVALID;
    struct fnv_pcm_queue *q=calloc(1,sizeof(*q));if(!q)return FNV_PCM_NO_MEMORY;
    q->samples=calloc(capacity,4);if(!q->samples){free(q);return FNV_PCM_NO_MEMORY;}
    if(!fnv_host_mutex_init(&q->guard)){free(q->samples);free(q);return FNV_PCM_NO_MEMORY;}
    q->capacity=capacity;*out=q;return FNV_PCM_OK;
}
void fnv_pcm_destroy(struct fnv_pcm_queue *q){if(q){fnv_host_mutex_destroy(&q->guard);free(q->samples);free(q);}}
enum fnv_pcm_result fnv_pcm_write(struct fnv_pcm_queue *q,const int16_t *src,uint32_t frames){
    if(!q || (!src && frames) || frames>q->capacity)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&q->guard);
    if(frames>q->capacity-q->count){q->full_writes++;fnv_host_mutex_unlock(&q->guard);return FNV_PCM_FULL;}
    uint32_t at=(q->head+q->count)%q->capacity,first=q->capacity-at;if(first>frames)first=frames;
    if(first)memcpy(q->samples+at*2,src,(size_t)first*4);
    if(frames>first)memcpy(q->samples,src+first*2,(size_t)(frames-first)*4);
    q->count+=frames;q->written+=frames;fnv_host_mutex_unlock(&q->guard);return FNV_PCM_OK;
}
uint32_t fnv_pcm_read(struct fnv_pcm_queue *q,int16_t *dst,uint32_t frames){
    if(!q || !dst || frames>48000)return 0;
    fnv_host_mutex_lock(&q->guard);uint32_t count=q->count<frames?q->count:frames,first=q->capacity-q->head;
    if(first>count)first=count;
    if(first)memcpy(dst,q->samples+q->head*2,(size_t)first*4);
    if(count>first)memcpy(dst+first*2,q->samples,(size_t)(count-first)*4);
    if(frames>count)memset(dst+count*2,0,(size_t)(frames-count)*4);
    q->head=(q->head+count)%q->capacity;q->count-=count;q->read+=count;q->silence+=frames-count;
    fnv_host_mutex_unlock(&q->guard);return count;
}
enum fnv_pcm_result fnv_pcm_stats(struct fnv_pcm_queue *q,struct fnv_pcm_stats *out){
    if(!q || !out)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&q->guard);*out=(struct fnv_pcm_stats){q->count,q->capacity,q->written,q->read,q->silence,q->full_writes};
    fnv_host_mutex_unlock(&q->guard);return FNV_PCM_OK;
}
