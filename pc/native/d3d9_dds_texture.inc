/* Resource-producing D3DX belongs to the COM device module. Uses its existing
 * allocator, backend object, shadows and reference accounting, not a parallel
 * texture registry. Only complete POT 2D DDS chains need no filtering. */
#include "dds_image_info.h"
static void api_dds_texture(void){
    uint32_t self=fnv_cpu_argument_u32(0),src=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2),at=fnv_cpu_argument_u32(3);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,at,4,1);
    if(!self || !src || !bytes || !out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,16);return;}
    const unsigned char *data=range(c,src,bytes,0);if(!data)invalid("D3DX texture image memory unavailable",src);
    unsigned char info[28];uint32_t result=fnv_dds_image_info(data,bytes,info);
    if(result==FNV_DDS_UNSUPPORTED)invalid("D3DX texture DDS layout unsupported",src);
    if(result){
        if((bytes>=8 && !memcmp(data,"\x89PNG\r\n\x1a\n",8)) || (bytes>=2 && ((data[0]=='B' && data[1]=='M') || (data[0]==0xff && data[1]==0xd8))))
            invalid("D3DX texture image codec pending",src);
        fnv_cpu_return_u32(result,16);return;
    }
    uint32_t width=read32(info),height=read32(info+4),levels=read32(info+12),format=read32(info+16),block,unit,maximum=1;
    if(read32(info+20)!=3 || (width&(width-1)) || (height&(height-1)) || width>16384 || height>16384)
        invalid("D3DX texture resampling or resource shape pending",src);
    for(uint32_t n=width>height?width:height;n>1;n>>=1)maximum++;
    if(levels!=maximum)invalid("D3DX texture mip generation pending",src);
    if(format==0x32545844 || format==0x34545844)invalid("D3DX premultiplied DDS conversion pending",format);
    if(!texture_format(format,&block,&unit))invalid("D3DX texture format conversion pending",format);
    if(!c->config.backend.create_texture || !c->config.backend.upload_texture || !c->buffer_memory)
        invalid("D3DX texture backend or shadow arena pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);fnv_host_mutex_unlock(&c->guard);
    /* Input may alias the dedicated shadow pool or output. Keep it stable
     * across allocation and all backend calls, without retaining guest data. */
    unsigned char *snapshot=malloc(bytes);if(!snapshot){fnv_cpu_return_u32(FNV_D3D9_OUTOFMEMORY,16);return;}memcpy(snapshot,data,bytes);
    uint32_t texture=0;result=create_texture(c,self,width,height,levels,0,format,1,0,&texture);
    if(!result){
        fnv_host_mutex_lock(&c->guard);int index=object(c,texture,0);struct resource *r=c->resources+index;
        uint32_t source=128;
        for(unsigned level=0;level<levels;++level){struct mip *m=r->mips+level;
            uint32_t rows=(m->height+block-1)/block,row=(m->width+block-1)/block*unit;
            unsigned char *shadow=c->buffer_memory+(r->data-c->config.buffer_address)+m->offset;
            memset(shadow,0,(size_t)m->pitch*rows);
            for(unsigned y=0;y<rows;++y)memcpy(shadow+y*m->pitch,snapshot+source+y*row,row);
            result=c->config.backend.upload_texture(c->config.backend.context,r->handle,level,0,0,m->width,m->height,0,m->pitch,shadow);
            if(result)break;
            r->version++;source+=row*rows;
        }
        if(result)decrement(c,index); /* Backend release, parent ref and shadow rollback. */
        fnv_host_mutex_unlock(&c->guard);
    }
    free(snapshot);
    if(!result)write32(out,texture); /* No partially initialized object escapes. */
    fnv_cpu_return_u32(result,16);
}
