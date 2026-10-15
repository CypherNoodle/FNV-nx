#include "d3d9_constants.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct bank { uint32_t *data;uint64_t dirty[4];uint32_t count,width; };
struct fnv_d3d9_constants {
    struct bank banks[2][3];uint32_t floats[2][256*4],integers[2][16*4],booleans[2][16];
    fnv_host_mutex guard;
};
static void dirty(struct bank *b,uint32_t i) {b->dirty[i/64]|=UINT64_C(1)<<(i&63);}
static int is_dirty(const struct bank *b,uint32_t i) {return !!(b->dirty[i/64]&(UINT64_C(1)<<(i&63)));}
static struct bank *select_bank(struct fnv_d3d9_constants *c,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const void *data) {
    if(!c || (unsigned)s>1 || (unsigned)k>2 || (count && !data))return NULL;
    struct bank *b=&c->banks[s][k];
    if(first>b->count || count>b->count-first)return NULL;
    return b;
}
uint32_t fnv_d3d9_constants_create(uint32_t vf,uint32_t pf,struct fnv_d3d9_constants **out) {
    if(!out)return FNV_D3D9_INVALIDCALL;
    *out=NULL;if(!vf || vf>256 || !pf || pf>224)return FNV_D3D9_INVALIDCALL;
    struct fnv_d3d9_constants *c=calloc(1,sizeof(*c));if(!c)return FNV_D3D9_OUTOFMEMORY;
    if(!fnv_host_mutex_init(&c->guard)){free(c);return FNV_D3D9_OUTOFMEMORY;}
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++){
        struct bank *b=&c->banks[s][k];b->count=k?16:s?pf:vf;b->width=k==2?1:4;
        b->data=k==0?c->floats[s]:k==1?c->integers[s]:c->booleans[s];
        for(uint32_t i=0;i<b->count;i++)dirty(b,i);
    }
    *out=c;return FNV_D3D9_OK;
}
void fnv_d3d9_constants_destroy(struct fnv_d3d9_constants *c) {
    if(c){fnv_host_mutex_destroy(&c->guard);free(c);}
}
uint32_t fnv_d3d9_constants_set(struct fnv_d3d9_constants *c,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const void *bits) {
    struct bank *b=select_bank(c,s,k,first,count,bits);if(!b)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);
    for(uint32_t i=0;i<count;i++) {
        uint32_t value[4];memcpy(value,(const unsigned char*)bits+(size_t)i*b->width*4,b->width*4);
        uint32_t *at=b->data+(first+i)*b->width;
        if(memcmp(at,value,b->width*4)){memcpy(at,value,b->width*4);dirty(b,first+i);}
    }
    fnv_host_mutex_unlock(&c->guard);return FNV_D3D9_OK;
}
uint32_t fnv_d3d9_constants_get(struct fnv_d3d9_constants *c,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,void *bits) {
    struct bank *b=select_bank(c,s,k,first,count,bits);if(!b)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);if(count)memcpy(bits,b->data+first*b->width,(size_t)count*b->width*4);
    fnv_host_mutex_unlock(&c->guard);return FNV_D3D9_OK;
}
uint32_t fnv_d3d9_constants_flush(struct fnv_d3d9_constants *c,fnv_d3d9_constants_upload upload,void *opaque) {
    if(!c || !upload)return FNV_D3D9_INVALIDCALL;
    uint32_t result=FNV_D3D9_OK;fnv_host_mutex_lock(&c->guard);
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++) {
        struct bank *b=&c->banks[s][k];
        for(uint32_t i=0;i<b->count;) {
            if(!is_dirty(b,i)){i++;continue;}
            uint32_t end=i+1;while(end<b->count && is_dirty(b,end))end++;
            if(!upload(opaque,(enum fnv_d3d9_stage)s,(enum fnv_d3d9_constant_kind)k,i,end-i,b->data+i*b->width)){
                result=FNV_D3D9_UPLOAD_FAILED;goto done;
            }
            for(;i<end;i++)b->dirty[i/64]&=~(UINT64_C(1)<<(i&63));
        }
    }
done:fnv_host_mutex_unlock(&c->guard);return result;
}
void fnv_d3d9_constants_invalidate(struct fnv_d3d9_constants *c) {
    if(!c)return;
    fnv_host_mutex_lock(&c->guard);
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++)for(uint32_t i=0;i<c->banks[s][k].count;i++)dirty(&c->banks[s][k],i);
    fnv_host_mutex_unlock(&c->guard);
}
