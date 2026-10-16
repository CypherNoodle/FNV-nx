#include "d3d9_texture_upload.h"
#define INVALID 0x8876086cu
uint32_t fnv_d3d9_texture_rgba8(const struct fnv_texture_patch *input,void *destination,size_t destination_bytes,uint32_t destination_pitch){
    if(!input || !destination)return INVALID;
    struct fnv_texture_patch p=*input;
    unsigned unit;
    switch(p.format){case 21:case 22:unit=4;break;case 23:unit=2;break;case 28:case 50:unit=1;break;default:return INVALID;}
    if(!p.source || !p.width || !p.height || !p.copy_width || !p.copy_height ||
       p.left>=p.width || p.top>=p.height || p.copy_width>p.width-p.left || p.copy_height>p.height-p.top)return INVALID;
    uint64_t row=(uint64_t)p.copy_width*unit,dest_row=(uint64_t)p.width*4;
    if(row>p.source_pitch || dest_row>destination_pitch)return INVALID;
    uint64_t source_end=(uint64_t)(p.copy_height-1)*p.source_pitch+row;
    uint64_t dest_end=(uint64_t)(p.height-1)*destination_pitch+dest_row;
    uintptr_t src=(uintptr_t)p.source,dst=(uintptr_t)destination;
    if(source_end>p.source_bytes || dest_end>destination_bytes || source_end>UINTPTR_MAX-src || dest_end>UINTPTR_MAX-dst)return INVALID;
    if(src<dst+dest_end && dst<src+source_end)return INVALID;
    for(uint32_t y=0;y<p.copy_height;++y){
        const unsigned char *s=(const unsigned char *)p.source+(size_t)y*p.source_pitch;
        unsigned char *d=(unsigned char *)destination+(size_t)(p.top+y)*destination_pitch+(size_t)p.left*4;
        for(uint32_t x=0;x<p.copy_width;++x,s+=unit,d+=4){
            switch(p.format){
            case 21:case 22:d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=p.format==21?s[3]:255;break;
            case 23:{unsigned v=s[0]|(unsigned)s[1]<<8;
                d[0]=(unsigned char)(((v>>11)*255+15)/31);
                d[1]=(unsigned char)((((v>>5)&63)*255+31)/63);
                d[2]=(unsigned char)(((v&31)*255+15)/31);d[3]=255;break;}
            case 28:d[0]=d[1]=d[2]=0;d[3]=s[0];break;
            case 50:d[0]=d[1]=d[2]=s[0];d[3]=255;break;
            }
        }
    }
    return 0;
}
