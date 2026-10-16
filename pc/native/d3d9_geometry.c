#include "d3d9_geometry.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>
#define INVALID 0x8876086cu
static uint32_t le16(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8;}
uint32_t fnv_d3d9_geometry_validate(const struct fnv_geometry_input *in,struct fnv_geometry_access *out){
    static const unsigned sizes[17]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    if(!in || !out || !in->elements || in->element_count<2 || in->element_count>65 ||
       in->indexed>1 || !in->primitives)return INVALID;
    uint64_t count;
    switch(in->primitive){
    case 1:count=in->primitives;break;
    case 2:count=(uint64_t)in->primitives*2;break;
    case 3:count=(uint64_t)in->primitives+1;break;
    case 4:count=(uint64_t)in->primitives*3;break;
    case 5:case 6:count=(uint64_t)in->primitives+2;break;
    default:return INVALID;
    }
    if(count>UINT32_MAX)return INVALID;
    struct fnv_geometry_access result={0};result.count=(uint32_t)count;
    uint64_t low=in->start,high=low+count-1;
    if(in->indexed){
        if(!in->indices || (in->index_size!=2 && in->index_size!=4) || !in->vertices)return INVALID;
        uint64_t offset=(uint64_t)in->start*in->index_size,bytes=count*in->index_size;
        if(offset>in->index_bytes || bytes>in->index_bytes-offset || offset+bytes>SIZE_MAX)return INVALID;
        const unsigned char *p=(const unsigned char *)in->indices+(size_t)offset;
        low=UINT32_MAX;high=0;
        for(uint64_t i=0;i<count;++i,p+=in->index_size){
            uint32_t value=le16(p);if(in->index_size==4)value|=le16(p+2)<<16;
            if(value<in->min_vertex || (uint64_t)value>=(uint64_t)in->min_vertex+in->vertices)return INVALID;
            if(value<low)low=value;
            if(value>high)high=value;
        }
        int64_t base=(in->base_bits&0x80000000u)?(int64_t)in->base_bits-0x100000000ll:in->base_bits;
        int64_t first=(int64_t)low+base,last=(int64_t)high+base;
        if(first<0 || last>UINT32_MAX)return INVALID;
        low=(uint64_t)first;high=(uint64_t)last;result.indices=(struct fnv_geometry_range){offset,bytes};
    }
    if(high>UINT32_MAX)return INVALID;
    result.first_vertex=(uint32_t)low;result.last_vertex=(uint32_t)high;
    unsigned min_offset[16],max_end[16]={0};for(unsigned s=0;s<16;++s)min_offset[s]=UINT32_MAX;
    const unsigned char *e=in->elements;
    for(unsigned i=0;i<in->element_count;++i,e+=8){
        unsigned stream=le16(e),offset=le16(e+2),type=e[4];
        if(i==in->element_count-1){
            if(stream!=255 || offset || type!=17 || e[5] || e[6] || e[7])return INVALID;
            break;
        }
        if(stream>=16 || type>=17 || e[5] || e[6]>13 || e[7]>15)return INVALID;
        if(in->streams[stream].frequency!=1)return INVALID;
        /* Overlapping semantics cannot identify an unambiguous shader input. */
        const unsigned char *prior=in->elements;
        for(unsigned j=0;j<i;++j,prior+=8)if(prior[6]==e[6] && prior[7]==e[7])return INVALID;
        if(offset<min_offset[stream])min_offset[stream]=offset;
        if(offset+sizes[type]>max_end[stream])max_end[stream]=offset+sizes[type];
        result.stream_mask|=1u<<stream;
    }
    for(unsigned s=0;s<16;++s)if(result.stream_mask&(1u<<s)){
        const struct fnv_geometry_stream *stream=in->streams+s;
        uint64_t first=(uint64_t)stream->offset+low*stream->stride+min_offset[s];
        uint64_t end=(uint64_t)stream->offset+high*stream->stride+max_end[s];
        if(end>stream->bytes || end>SIZE_MAX)return INVALID;
        result.streams[s]=(struct fnv_geometry_range){first,end-first};
    }
    *out=result;return 0;
}
