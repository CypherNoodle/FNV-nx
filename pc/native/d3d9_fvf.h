#ifndef FNV_D3D9_FVF_H
#define FNV_D3D9_FVF_H
#include <stdint.h>
#include <string.h>
struct fnv_fvf_layout {unsigned char elements[16*8];uint32_t count,stride;};
static inline void fnv_fvf_element(struct fnv_fvf_layout *p,unsigned type,unsigned usage,unsigned index,unsigned bytes){
    unsigned char *e=p->elements+p->count++*8;e[2]=(unsigned char)p->stride;e[3]=(unsigned char)(p->stride>>8);e[4]=(unsigned char)type;e[6]=(unsigned char)usage;e[7]=(unsigned char)index;p->stride+=bytes;
}
/* Strict unblended FVF subset; packed little-endian D3DVERTEXELEMENT9 output.
 * Output is unchanged on failure. This describes data, not fixed-function VS. */
static inline uint32_t fnv_d3d9_fvf_layout(uint32_t fvf,struct fnv_fvf_layout *out){
    if(!out || (fvf&0x0000b001u))return 0x8876086cu;
    unsigned position=fvf&0x400eu,tex=(fvf>>8)&15u;
    if(tex>8 || (position!=0 && position!=2 && position!=4 && position!=0x4002) || (position==4 && (fvf&16)))return 0x8876086cu;
    if(tex<8 && (fvf>>(16+tex*2)))return 0x8876086cu;
    struct fnv_fvf_layout p={0};
    if(position)fnv_fvf_element(&p,position==2?2:3,position==4?9:0,0,position==2?12:16);
    if(fvf&16)fnv_fvf_element(&p,2,3,0,12);
    if(fvf&32)fnv_fvf_element(&p,0,4,0,4);
    if(fvf&64)fnv_fvf_element(&p,4,10,0,4);
    if(fvf&128)fnv_fvf_element(&p,4,10,1,4);
    const unsigned dimensions[4]={2,3,4,1};
    for(unsigned i=0;i<tex;++i){unsigned size=dimensions[(fvf>>(16+i*2))&3];fnv_fvf_element(&p,size-1,5,i,size*4);}
    unsigned char *end=p.elements+p.count++*8;end[0]=255;end[4]=17;*out=p;return 0;
}
#endif
