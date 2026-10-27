/* d->guard protects every ring and generation; callers do not hold it. */
static struct music_ring *music_token(struct fnv_dsound8 *d,uint32_t token){
    unsigned slot=token&7u;struct music_ring *m=d->music+slot;
    return m->pcm && (token>>3)==m->generation?m:NULL;
}
enum fnv_pcm_result fnv_dsound_stream_open(struct fnv_dsound8 *d,uint32_t *out){
    if(!d || d!=active || !out)return FNV_PCM_INVALID;
    *out=0;int16_t *pcm=malloc(MOVIE_FRAMES*4);if(!pcm)return FNV_PCM_NO_MEMORY;
    fnv_host_mutex_lock(&d->guard);
    for(unsigned i=0;i<8;i++)if(!d->music[i].pcm && d->music[i].generation<0x1fffffffu){
        struct music_ring *m=d->music+i;uint32_t generation=m->generation+1;
        memset(m,0,sizeof(*m));m->generation=generation;m->pcm=pcm;m->paused=1;m->gain[0]=m->gain[1]=65536;*out=generation*8+i;
        fnv_host_mutex_unlock(&d->guard);return FNV_PCM_OK;
    }
    fnv_host_mutex_unlock(&d->guard);free(pcm);return FNV_PCM_FULL;
}
enum fnv_pcm_result fnv_dsound_stream_close(struct fnv_dsound8 *d,uint32_t token){
    if(!d || d!=active)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&d->guard);struct music_ring *m=music_token(d,token);
    if(!m){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_INVALID;}
    int16_t *pcm=m->pcm;m->pcm=NULL;m->head=m->count=0;
    fnv_host_mutex_unlock(&d->guard);free(pcm);return FNV_PCM_OK;
}
enum fnv_pcm_result fnv_dsound_stream_write(struct fnv_dsound8 *d,uint32_t token,const int16_t *pcm,uint32_t frames){
    if(!d || d!=active || !pcm || !frames || frames>MOVIE_FRAMES)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&d->guard);struct music_ring *m=music_token(d,token);
    if(!m){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_INVALID;}
    if(frames>MOVIE_FRAMES-m->count){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_FULL;}
    uint32_t tail=(m->head+m->count)%MOVIE_FRAMES,first=frames<MOVIE_FRAMES-tail?frames:MOVIE_FRAMES-tail;
    memcpy(m->pcm+tail*2,pcm,first*4);memcpy(m->pcm,pcm+first*2,(frames-first)*4);m->count+=frames;
    fnv_host_mutex_unlock(&d->guard);return FNV_PCM_OK;
}
enum fnv_pcm_result fnv_dsound_stream_control(struct fnv_dsound8 *d,uint32_t token,int paused,int32_t volume,int32_t balance){
    if(!d || d!=active || (paused!=0 && paused!=1) || volume< -10000 || volume>0 || balance< -10000 || balance>10000)return FNV_PCM_INVALID;
    int32_t gain=volume== -10000?0:(int32_t)llround(pow(10.0,(double)volume/2000.0)*65536);
    int32_t left=balance>0?(balance==10000?0:(int32_t)llround(pow(10.0,-(double)balance/2000.0)*65536)):65536;
    int32_t right=balance<0?(balance== -10000?0:(int32_t)llround(pow(10.0,(double)balance/2000.0)*65536)):65536;
    fnv_host_mutex_lock(&d->guard);struct music_ring *m=music_token(d,token);
    if(!m){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_INVALID;}
    m->paused=(uint32_t)paused;m->gain[0]=(int32_t)((int64_t)gain*left/65536);m->gain[1]=(int32_t)((int64_t)gain*right/65536);
    fnv_host_mutex_unlock(&d->guard);return FNV_PCM_OK;
}
enum fnv_pcm_result fnv_dsound_stream_reset(struct fnv_dsound8 *d,uint32_t token){
    if(!d || d!=active)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&d->guard);struct music_ring *m=music_token(d,token);
    if(!m){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_INVALID;}
    m->head=m->count=0;m->played=0;fnv_host_mutex_unlock(&d->guard);return FNV_PCM_OK;
}
enum fnv_pcm_result fnv_dsound_stream_stats(struct fnv_dsound8 *d,uint32_t token,struct fnv_dsound_stream_stats *s){
    if(!d || d!=active || !s)return FNV_PCM_INVALID;
    fnv_host_mutex_lock(&d->guard);struct music_ring *m=music_token(d,token);
    if(!m){fnv_host_mutex_unlock(&d->guard);return FNV_PCM_INVALID;}
    *s=(struct fnv_dsound_stream_stats){m->count,MOVIE_FRAMES,m->paused,m->played};
    fnv_host_mutex_unlock(&d->guard);return FNV_PCM_OK;
}
