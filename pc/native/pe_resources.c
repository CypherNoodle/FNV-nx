/* Independent bounded lookup of numeric PE32 resource directories. */
#include "pe_resources.h"
#include <string.h>
static uint16_t read_u16(const unsigned char *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t read_u32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static const unsigned char *at(const struct fnv_native_image *image,uint32_t rva,uint32_t bytes){
    uint64_t va=(uint64_t)image->base+rva;
    return va<=UINT32_MAX?fnv_native_guest_range(image,(uint32_t)va,bytes):NULL;
}
struct resource_tree {const struct fnv_native_image *image;uint32_t rva,bytes;};
static const unsigned char *node(const struct resource_tree *t,uint32_t offset,uint32_t bytes){
    if(offset>t->bytes || bytes>t->bytes-offset || (uint64_t)t->rva+offset>UINT32_MAX)return NULL;
    return at(t->image,t->rva+offset,bytes);
}
static int entries(const struct resource_tree *t,uint32_t offset,const unsigned char **items,uint32_t *count){
    const unsigned char *h=node(t,offset,16);if(!h)return 0;
    *count=(uint32_t)read_u16(h+12)+read_u16(h+14);
    if(*count>4096 || offset>UINT32_MAX-16)return 0;
    *items=node(t,offset+16,*count*8);return *items!=NULL;
}
static enum fnv_resource_result child(const struct resource_tree *t,uint32_t offset,uint16_t id,uint32_t *out){
    const unsigned char *items;uint32_t count,found=0;
    if(!entries(t,offset,&items,&count))return FNV_RESOURCE_INVALID;
    for(uint32_t i=0;i<count;i++)if(read_u32(items+i*8)==id){*out=read_u32(items+i*8+4);found++;}
    if(found>1)return FNV_RESOURCE_INVALID;
    return found?FNV_RESOURCE_OK:FNV_RESOURCE_ABSENT;
}
enum fnv_resource_result fnv_pe_resource_find(const struct fnv_native_image *image,uint16_t type,uint16_t id,uint16_t language,struct fnv_pe_resource *out){
    if(!out)return FNV_RESOURCE_INVALID;
    memset(out,0,sizeof(*out));if(!image || !image->data)return FNV_RESOURCE_INVALID;
    const unsigned char *dos=at(image,0,64);if(!dos || dos[0]!='M' || dos[1]!='Z')return FNV_RESOURCE_INVALID;
    uint32_t nt=read_u32(dos+60);const unsigned char *pe=at(image,nt,24);
    if(!pe || memcmp(pe,"PE\0\0",4) || read_u16(pe+20)<120 || nt>UINT32_MAX-24)return FNV_RESOURCE_INVALID;
    const unsigned char *optional=at(image,nt+24,120);
    if(!optional || read_u16(optional)!=0x10b)return FNV_RESOURCE_INVALID;
    if(read_u32(optional+92)<3)return FNV_RESOURCE_ABSENT;
    struct resource_tree t={image,read_u32(optional+112),read_u32(optional+116)};
    if(!t.rva && !t.bytes)return FNV_RESOURCE_ABSENT;
    if(!t.rva || t.bytes<16 || !at(image,t.rva,t.bytes))return FNV_RESOURCE_INVALID;
    uint32_t offset=0;
    const uint16_t path[]={type,id};
    for(unsigned i=0;i<2;i++){
        uint32_t next=0;enum fnv_resource_result r=child(&t,offset,path[i],&next);
        if(r!=FNV_RESOURCE_OK)return r;
        if(!(next&0x80000000u))return FNV_RESOURCE_INVALID;
        offset=next&0x7fffffffu;
    }
    const unsigned char *items;uint32_t count;
    if(!entries(&t,offset,&items,&count))return FNV_RESOURCE_INVALID;
    uint32_t best=UINT32_MAX,data_entry=0;unsigned score=4;
    for(uint32_t i=0;i<count;i++){
        uint32_t lang=read_u32(items+i*8);if(lang&0x80000000u)continue;
        if(lang>65535)return FNV_RESOURCE_INVALID;
        unsigned rank=lang==language?0:lang==0?1:2;
        if(rank<score || (rank==score && lang<best)){best=lang;score=rank;data_entry=read_u32(items+i*8+4);}
        else if(lang==best)return FNV_RESOURCE_INVALID;
    }
    if(best==UINT32_MAX)return FNV_RESOURCE_ABSENT;
    if(data_entry&0x80000000u)return FNV_RESOURCE_INVALID;
    const unsigned char *data=node(&t,data_entry,16);if(!data || read_u32(data+12))return FNV_RESOURCE_INVALID;
    uint32_t rva=read_u32(data),bytes=read_u32(data+4);const unsigned char *payload=at(image,rva,bytes);
    if(!bytes || !payload)return FNV_RESOURCE_INVALID;
    *out=(struct fnv_pe_resource){payload,bytes,read_u32(data+8),(uint16_t)best};return FNV_RESOURCE_OK;
}
