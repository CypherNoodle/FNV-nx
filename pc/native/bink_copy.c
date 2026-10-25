/* Own bounded copy from decoder RGBA to the initial Bink packed formats. */
#include "bink_copy.h"
#include <stdint.h>
enum fnv_bink_copy_result fnv_bink_copy_rect(const struct fnv_bink_frame *f,size_t source_bytes,
    void *destination,size_t destination_bytes,int32_t pitch,uint32_t destination_height,
    uint32_t dx,uint32_t dy,uint32_t sx,uint32_t sy,uint32_t width,uint32_t height,uint32_t flags){
    uint32_t format=flags&255u,scale=(flags>>28)&7u;
    if((flags&~0xf00000ffu) || (format!=3 && format!=4) || (scale!=0 && scale!=7))return FNV_BINK_COPY_UNSUPPORTED;
    if(!f || !f->rgba || !destination || pitch<=0 || !destination_height || !width || !height ||
       !f->width || !f->height || (uint64_t)f->width*4>f->pitch ||
       sx>=f->width || sy>=f->height)return FNV_BINK_COPY_INVALID;
    /* Bink clips edge tiles to the decoded image, not to the caller buffer. */
    if(width>f->width-sx)width=f->width-sx;
    if(height>f->height-sy)height=f->height-sy;
    if(dy>destination_height || height>destination_height-dy ||
       ((uint64_t)dx+width)*4>(uint32_t)pitch)return FNV_BINK_COPY_INVALID;
    uint64_t source_span=(uint64_t)(f->height-1)*f->pitch+(uint64_t)f->width*4;
    uint64_t target_span=(uint64_t)destination_height*(uint32_t)pitch;
    if(source_span>source_bytes || target_span>destination_bytes)return FNV_BINK_COPY_INVALID;
    uintptr_t a=(uintptr_t)f->rgba,b=(uintptr_t)destination;
    if((a<=b && b-a<source_span) || (a>b && a-b<target_span))return FNV_BINK_COPY_INVALID;
    unsigned char *dst=destination;
    for(uint32_t y=0;y<height;y++){
        const unsigned char *s=f->rgba+(size_t)(sy+y)*f->pitch+(size_t)sx*4;
        unsigned char *d=dst+(size_t)(dy+y)*(uint32_t)pitch+(size_t)dx*4;
        for(uint32_t x=0;x<width;x++,s+=4,d+=4){d[0]=s[format==3?2:0];d[1]=s[1];d[2]=s[format==3?0:2];d[3]=0;}
    }
    return FNV_BINK_COPY_OK;
}
