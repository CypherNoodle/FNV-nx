#include "pcm_convert.h"
#include <string.h>
enum fnv_convert_result fnv_pcm_convert_init(struct fnv_pcm_converter *c,uint32_t rate){
    if(!c || rate<8000 || rate>48000)return FNV_CONVERT_INVALID;
    memset(c,0,sizeof(*c));c->rate=rate;return FNV_CONVERT_OK;
}
static int next(struct fnv_pcm_converter *c,fnv_pcm_source source,void *context){
    int r=source(context,c->b);
    if(r!=0 && r!=1){c->failed=1;return 0;}
    c->b_real=r;if(!r)memcpy(c->b,c->a,sizeof(c->a));return 1;
}
enum fnv_convert_result fnv_pcm_convert_read(struct fnv_pcm_converter *c,fnv_pcm_source source,void *context,int16_t *out,uint32_t capacity,uint32_t *frames){
    if(!frames)return FNV_CONVERT_INVALID;
    *frames=0;
    if(!c || !source || (!out && capacity) || capacity>48000 || c->rate<8000 || c->rate>48000)return FNV_CONVERT_INVALID;
    if(c->failed)return FNV_CONVERT_SOURCE_ERROR;
    if(!capacity)return FNV_CONVERT_OK;
    if(!c->initialized){
        int r=source(context,c->a);c->initialized=1;c->have=r==1;
        if(r!=0 && r!=1){c->failed=1;return FNV_CONVERT_SOURCE_ERROR;}
        if(c->have && !next(c,source,context))return FNV_CONVERT_SOURCE_ERROR;
    }
    while(*frames<capacity && c->have){
        for(unsigned i=0;i<2;i++){
            int64_t n=(int64_t)c->a[i]*(48000-c->phase)+(int64_t)c->b[i]*c->phase;
            out[*frames*2+i]=(int16_t)(n>=0?(n+24000)/48000:-((-n+24000)/48000));
        }
        (*frames)++;c->phase+=c->rate;
        if(c->phase>=48000){
            c->phase-=48000;
            if(!c->b_real){c->have=0;break;}
            memcpy(c->a,c->b,sizeof(c->a));if(!next(c,source,context))return FNV_CONVERT_SOURCE_ERROR;
        }
    }
    return *frames?FNV_CONVERT_OK:FNV_CONVERT_EOF;
}
