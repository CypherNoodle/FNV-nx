#include <switch.h>
#include "media_horizon.h"
#include "host_thread.h"
#include <malloc.h>
#include <stdatomic.h>
#include <stdlib.h>
enum {BUFFERS=3,FRAMES=512};
struct fnv_horizon_audio {struct fnv_pcm_queue *queue;AudioOutBuffer buffers[BUFFERS];fnv_host_thread worker;atomic_uint stop,error;atomic_uint_fast64_t refills,underrun_frames,wait_timeouts;int started;uint32_t (*produce)(void *,uint32_t);void *context;};
static struct fnv_horizon_audio *active_audio; /* Host serializes setup/teardown. */
static uint32_t fill(struct fnv_horizon_audio *a,AudioOutBuffer *b){
    if(a->produce){uint32_t rc=a->produce(a->context,FRAMES);if(rc)return rc;}
    uint32_t read=fnv_pcm_read(a->queue,b->buffer,FRAMES);
    atomic_fetch_add_explicit(&a->refills,1,memory_order_relaxed);
    atomic_fetch_add_explicit(&a->underrun_frames,FRAMES-read,memory_order_relaxed);
    armDCacheFlush(b->buffer,FRAMES*4);return 0;
}
static void worker(void *context){
    struct fnv_horizon_audio *a=context;
    while(!atomic_load_explicit(&a->stop,memory_order_acquire)){
        AudioOutBuffer *released=NULL;u32 count=0;Result rc=audoutWaitPlayFinish(&released,&count,20000000);
        if(rc==KERNELRESULT(TimedOut)){atomic_fetch_add_explicit(&a->wait_timeouts,1,memory_order_relaxed);continue;}
        if(R_FAILED(rc)){atomic_store(&a->error,rc);break;}
        if(!count)continue;
        int known=0;for(unsigned i=0;i<BUFFERS;i++)if(released==a->buffers+i)known=1;
        if(!known || count!=1){atomic_store(&a->error,MAKERESULT(Module_Libnx,LibnxError_BadInput));break;}
        rc=fill(a,released);
        if(R_FAILED(rc)){atomic_store(&a->error,rc);break;}
        rc=audoutAppendAudioOutBuffer(released);
        if(R_FAILED(rc)){atomic_store(&a->error,rc);break;}
    }
}
uint32_t fnv_horizon_audio_open_source(struct fnv_pcm_queue *queue,uint32_t (*produce)(void *,uint32_t),void *context,struct fnv_horizon_audio **out){
    if(!out || !queue)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    *out=NULL;if(active_audio)return MAKERESULT(Module_Libnx,LibnxError_AlreadyInitialized);
    struct fnv_horizon_audio *a=calloc(1,sizeof(*a));if(!a)return MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);
    a->queue=queue;a->produce=produce;a->context=context;atomic_init(&a->stop,0);atomic_init(&a->error,0);
    atomic_init(&a->refills,0);atomic_init(&a->underrun_frames,0);atomic_init(&a->wait_timeouts,0);
    Result rc=audoutInitialize();if(R_FAILED(rc)){free(a);return rc;}
    if(audoutGetSampleRate()!=48000 || audoutGetChannelCount()!=2 || audoutGetPcmFormat()!=PcmFormat_Int16){rc=MAKERESULT(Module_Libnx,LibnxError_BadInput);goto fail;}
    for(unsigned i=0;i<BUFFERS;i++){
        AudioOutBuffer *b=a->buffers+i;b->buffer=memalign(4096,4096);if(!b->buffer){rc=MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);goto fail;}
        b->buffer_size=4096;b->data_size=FRAMES*4;rc=fill(a,b);if(R_FAILED(rc))goto fail;
        rc=audoutAppendAudioOutBuffer(b);if(R_FAILED(rc))goto fail;
    }
    rc=audoutStartAudioOut();if(R_FAILED(rc))goto fail;
    if(!fnv_host_thread_create(&a->worker,worker,a,65536)){rc=MAKERESULT(Module_Libnx,LibnxError_OutOfMemory);goto fail;}
    a->started=1;active_audio=a;*out=a;return 0;
fail:
    (void)audoutStopAudioOut();audoutExit();for(unsigned i=0;i<BUFFERS;i++)free(a->buffers[i].buffer);free(a);return rc;
}
uint32_t fnv_horizon_audio_open(struct fnv_pcm_queue *queue,struct fnv_horizon_audio **out){
    return fnv_horizon_audio_open_source(queue,NULL,NULL,out);
}
uint32_t fnv_horizon_audio_error(struct fnv_horizon_audio *a){return a?atomic_load(&a->error):MAKERESULT(Module_Libnx,LibnxError_BadInput);}
uint32_t fnv_horizon_audio_metrics(struct fnv_horizon_audio *a,struct fnv_horizon_audio_metrics *out){
    if(!a || !out)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    *out=(struct fnv_horizon_audio_metrics){atomic_load_explicit(&a->refills,memory_order_relaxed),atomic_load_explicit(&a->underrun_frames,memory_order_relaxed),atomic_load_explicit(&a->wait_timeouts,memory_order_relaxed)};return 0;
}
uint32_t fnv_horizon_audio_close(struct fnv_horizon_audio *a){
    if(!a || active_audio!=a)return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    atomic_store_explicit(&a->stop,1,memory_order_release);
    if(a->started && !fnv_host_thread_join(&a->worker))return MAKERESULT(Module_Libnx,LibnxError_BadInput);
    (void)audoutStopAudioOut();audoutExit();for(unsigned i=0;i<BUFFERS;i++)free(a->buffers[i].buffer);active_audio=NULL;free(a);return 0;
}
