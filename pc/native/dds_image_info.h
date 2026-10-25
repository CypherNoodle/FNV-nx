#ifndef FNV_DDS_IMAGE_INFO_H
#define FNV_DDS_IMAGE_INFO_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* Own legacy DDS metadata parser, not a pixel decoder. HRESULT-compatible
 * invalid data versus explicit unsupported features. Output is staged. */
#define FNV_DDS_INVALID_DATA 0x88760b59u
#define FNV_DDS_UNSUPPORTED 0x80004001u
static inline uint32_t fnv_dds_u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline void fnv_dds_put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i));}
static inline uint32_t fnv_dds_image_info(const unsigned char *data,uint32_t bytes,unsigned char output[28]){
    if(bytes<128 || fnv_dds_u32(data)!=0x20534444u)return FNV_DDS_INVALID_DATA;
    if(fnv_dds_u32(data+76)!=32)return FNV_DDS_INVALID_DATA;
    uint32_t height=fnv_dds_u32(data+12),width=fnv_dds_u32(data+16),depth=1,mips=fnv_dds_u32(data+28);
    uint32_t flags=fnv_dds_u32(data+80),fourcc=fnv_dds_u32(data+84),bits=fnv_dds_u32(data+88),format=0,block=0,type=3,faces=1;
    uint32_t r=fnv_dds_u32(data+92),g=fnv_dds_u32(data+96),b=fnv_dds_u32(data+100),a=fnv_dds_u32(data+104);
    if(flags&4){if((fourcc&0xffffffu)==0x545844u && (fourcc>>24)>=0x31 && (fourcc>>24)<=0x35){format=fourcc;block=fourcc==0x31545844u?8:16;}else return FNV_DDS_UNSUPPORTED;}
    else if((flags&0x40) && bits==24 && r==0xff0000 && g==0xff00 && b==0xff && !a){format=20;}
    else if((flags&0x40) && bits==32 && r==0xff0000 && g==0xff00 && b==0xff && (a==0xff000000 || !a)){format=a?21:22;}
    else if((flags&0x40) && bits==32 && r==0xff && g==0xff00 && b==0xff0000 && (a==0xff000000 || !a)){format=a?32:33;}
    else if((flags&0x40) && bits==16 && r==0xf800 && g==0x7e0 && b==0x1f && !a){format=23;}
    else if((flags&0x40) && bits==16 && r==0x7c00 && g==0x3e0 && b==0x1f && (a==0x8000 || !a)){format=a?25:24;}
    else if((flags&0x40) && bits==16 && r==0xf00 && g==0xf0 && b==0xf && a==0xf000){format=26;}
    else if((flags&2) && bits==8 && a==0xff){format=28;}
    else if((flags&0x20000) && bits==8 && r==0xff && !g && !b && !a){format=50;}
    else if((flags&0x20000) && bits==16 && r==0xff && !g && !b && a==0xff00){format=51;}
    else return FNV_DDS_UNSUPPORTED;
    if(!width || !height || mips>256)return FNV_DDS_UNSUPPORTED;
    if(!mips)mips=1;
    uint32_t caps=fnv_dds_u32(data+112);
    if(fnv_dds_u32(data+8)&0x800000){depth=fnv_dds_u32(data+24);if(!depth)depth=1;type=4;}
    else if(caps&0xfc00){if((caps&0xfe00)!=0xfe00)return FNV_DDS_INVALID_DATA;type=5;faces=6;}
    uint64_t used=128,limit=bytes,w=width,h=height,d=depth;
    for(unsigned level=0;level<mips;++level){
        uint64_t rows=block?(h+3)/4:h,row=block?((w+3)/4)*block:(w*bits+7)/8;
        if(row>limit || rows>limit || d>limit || row*rows>limit || row*rows*d>limit || row*rows*d*faces>limit-used)return FNV_DDS_INVALID_DATA;
        used+=row*rows*d*faces;w=w>1?w/2:1;h=h>1?h/2:1;d=d>1?d/2:1;
    }
    const uint32_t values[]={width,height,depth,mips,format,type,4};
    for(unsigned i=0;i<7;++i)fnv_dds_put32(output+4*i,values[i]);
    return 0;
}
#endif
