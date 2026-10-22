#include "d3d9_sdp.h"
#include "d3d9_com.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDP_MAX (64u*1024u*1024u)
struct fnv_d3d9_sdp { size_t count;unsigned char *data;struct fnv_d3d9_shader_code *entries; };
static uint32_t word(const unsigned char *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
void fnv_sdp_destroy(struct fnv_d3d9_sdp *p) {if(p){free(p->entries);free(p->data);free(p);}}
const struct fnv_d3d9_shader_code *fnv_sdp_entries(const struct fnv_d3d9_sdp *p,size_t *count) {
    if(count)*count=p?p->count:0;
    return p?p->entries:NULL;
}
static enum fnv_sdp_result adopt(unsigned char *data,size_t size,size_t budget,struct fnv_d3d9_sdp **out) {
    enum fnv_sdp_result result=FNV_SDP_INVALID;
    struct fnv_d3d9_sdp *p=NULL;
    if(size<12 || size>SDP_MAX || word(data)!=100 || word(data+8)!=size-12)goto fail;
    size_t count=word(data+4);
    if(count>(size-12)/268)goto fail;
    if(count>8192 || size>budget || sizeof(*p)>budget-size || count>(budget-size-sizeof(*p))/sizeof(*p->entries)){result=FNV_SDP_LIMIT;goto fail;}
    p=calloc(1,sizeof(*p));if(!p){result=FNV_SDP_MEMORY;goto fail;}
    p->data=data;p->count=count;
    p->entries=count?calloc(count,sizeof(*p->entries)):NULL;
    if(count && !p->entries){result=FNV_SDP_MEMORY;goto fail;}
    size_t at=12;
    for(size_t i=0;i<count;i++) {
        if(size-at<260)goto fail;
        size_t n=0;
        while(n<256 && data[at+n]){if(data[at+n]<32 || data[at+n]>126)goto fail;n++;}
        if(!n || n==256)goto fail;
        size_t bytes=word(data+at+256);at+=260;
        if(bytes<8 || bytes%4 || bytes>size-at)goto fail;
        uint32_t v=word(data+at),stage=v>>16,major=(v>>8)&255;
        if((stage!=0xfffe && stage!=0xffff) || major<1 || major>3 || word(data+at+bytes-4)!=0xffff)goto fail;
        p->entries[i].bytes=data+at;p->entries[i].size=(uint32_t)bytes;at+=bytes;
    }
    if(at!=size)goto fail;
    *out=p;return FNV_SDP_OK;
fail:
    if(p)fnv_sdp_destroy(p);else free(data);
    return result;
}
enum fnv_sdp_result fnv_sdp_parse(const void *bytes,size_t size,size_t budget,struct fnv_d3d9_sdp **out) {
    if(!out)return FNV_SDP_INVALID;
    *out=NULL;
    if(!bytes || size<12 || size>SDP_MAX)return FNV_SDP_INVALID;
    if(size>budget)return FNV_SDP_LIMIT;
    unsigned char *data=malloc(size);if(!data)return FNV_SDP_MEMORY;
    memcpy(data,bytes,size);return adopt(data,size,budget,out);
}
enum fnv_sdp_result fnv_sdp_load(const char *path,size_t budget,struct fnv_d3d9_sdp **out) {
    if(!out)return FNV_SDP_INVALID;
    *out=NULL;if(!path)return FNV_SDP_INVALID;
    FILE *f=fopen(path,"rb");if(!f)return FNV_SDP_IO;
    unsigned char header[12];
    if(fread(header,1,12,f)!=12){fclose(f);return FNV_SDP_IO;}
    size_t payload=word(header+8);
    if(word(header)!=100 || payload>SDP_MAX-12){fclose(f);return FNV_SDP_INVALID;}
    size_t size=payload+12;
    if(size>budget){fclose(f);return FNV_SDP_LIMIT;}
    unsigned char *data=malloc(size);if(!data){fclose(f);return FNV_SDP_MEMORY;}
    memcpy(data,header,12);
    int valid=fread(data+12,1,payload,f)==payload;
    if(valid)valid=fgetc(f)==EOF && !ferror(f);
    if(fclose(f))valid=0;
    if(!valid){free(data);return FNV_SDP_IO;}
    return adopt(data,size,budget,out);
}
