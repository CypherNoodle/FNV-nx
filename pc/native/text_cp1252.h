#ifndef FNV_TEXT_CP1252_H
#define FNV_TEXT_CP1252_H
#include <stddef.h>
#include <stdint.h>
/* Windows-1252 text at the game boundary, UTF-8 at the native keyboard. */
static const uint16_t fnv_text_high[32]={0x20ac,0,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0,0x17d,0,0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0,0x17e,0x178};
static inline int fnv_text_to_utf8(const unsigned char *in,size_t length,char *out,size_t capacity){
    size_t n=0;if(!in || !out || !capacity)return 0;
    for(size_t i=0;i<length;i++){
        uint32_t c=in[i];if(c<32 || c==127)return 0;
        if(c>=128 && c<160)c=fnv_text_high[c-128];
        if(!c)return 0;
        size_t bytes=c<128?1:c<2048?2:3;if(bytes>=capacity-n)return 0;
        if(bytes==1)out[n++]=(char)c;
        else if(bytes==2){out[n++]=(char)(0xc0|(c>>6));out[n++]=(char)(0x80|(c&63));}
        else {out[n++]=(char)(0xe0|(c>>12));out[n++]=(char)(0x80|((c>>6)&63));out[n++]=(char)(0x80|(c&63));}
    }
    out[n]=0;return 1;
}
static inline int fnv_text_from_utf8(const char *in,unsigned char *out,size_t capacity){
    size_t n=0;if(!in || !out || !capacity)return 0;
    const unsigned char *p=(const unsigned char *)in;
    while(*p){
        uint32_t c=*p++;unsigned continuation=0;
        if(c>=0xc2 && c<=0xdf){c&=31;continuation=1;}
        else if(c>=0xe0 && c<=0xef){c&=15;continuation=2;}
        else if(c>=128)return 0;
        for(unsigned i=0;i<continuation;i++){if((*p&0xc0)!=0x80)return 0;c=(c<<6)|(*p++&63);}
        if((continuation==1 && c<128) || (continuation==2 && c<2048) || c<32 || c==127)return 0;
        unsigned value=0;
        if(c<128 || (c>=160 && c<=255))value=c;
        else for(unsigned i=0;i<32;i++)if(fnv_text_high[i]==c){value=128+i;break;}
        if(!value || n+1>=capacity)return 0;
        out[n++]=(unsigned char)value;
    }
    out[n]=0;return 1;
}
#endif
