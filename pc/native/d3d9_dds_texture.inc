/* Resource-producing D3DX belongs to the COM device module. Uses its existing
 * allocator, backend object, shadows and reference accounting, not a parallel
 * texture registry. Only complete POT 2D DDS chains need no filtering. */
#include "dds_image_info.h"
#include "bc_mip_chain.inc"
static void dds_texture(int cube){
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
    if(read32(info+20)!=(cube?5u:3u) || (cube && width!=height) || (width&(width-1)) || (height&(height-1)) || width>16384 || height>16384)
        invalid("D3DX texture resampling or resource shape pending",src);
    for(uint32_t n=width>height?width:height;n>1;n>>=1)maximum++;
    if((cube && levels!=maximum) || levels>maximum || (levels<maximum && format!=0x31545844 && format!=0x33545844 && format!=0x35545844))invalid("D3DX texture mip generation pending",src);
    if(format==0x32545844 || format==0x34545844)invalid("D3DX premultiplied DDS conversion pending",format);
    int rgb24=format==20;if(rgb24)format=22; /* Native D3DX expands BGR24 to X8R8G8B8. */
    if(!texture_format(format,&block,&unit))invalid("D3DX texture format conversion pending",format);
    if((cube?(!c->config.backend.create_cube_texture || !c->config.backend.upload_cube_texture):(!c->config.backend.create_texture || !c->config.backend.upload_texture)) || !c->buffer_memory)
        invalid("D3DX texture backend or shadow arena pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);fnv_host_mutex_unlock(&c->guard);
    /* Input may alias the dedicated shadow pool or output. Keep it stable
     * across allocation and all backend calls, without retaining guest data. */
    unsigned char *snapshot=NULL;
    if(levels<maximum){result=fnv_bc_complete_dds(data,bytes,width,height,levels,maximum,format,c->config.buffer_bytes,&snapshot,&bytes);
        if(result){fnv_cpu_return_u32(result,16);return;}levels=maximum;
    }else {snapshot=malloc(bytes);if(!snapshot){fnv_cpu_return_u32(FNV_D3D9_OUTOFMEMORY,16);return;}memcpy(snapshot,data,bytes);}
    uint32_t texture=0;result=create_texture_shape(c,self,width,height,levels,0,format,1,0,&texture,cube?6:1);
    if(!result){
        fnv_host_mutex_lock(&c->guard);int index=object(c,texture,0);struct resource *r=c->resources+index;
        uint32_t source=128;
        for(unsigned face=0;face<(cube?6u:1u) && !result;face++)for(unsigned level=0;level<levels;++level){struct mip *m=r->mips+face*levels+level;
            uint32_t rows=(m->height+block-1)/block,row=(m->width+block-1)/block*unit;
            unsigned char *shadow=c->buffer_memory+(r->data-c->config.buffer_address)+m->offset;
            memset(shadow,0,(size_t)m->pitch*rows);
            if(rgb24){
                row=m->width*3;
                for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<m->width;x++){
                    const unsigned char *pixel=snapshot+source+y*row+x*3;unsigned char *target=shadow+y*m->pitch+x*4;
                    target[0]=pixel[0];target[1]=pixel[1];target[2]=pixel[2];target[3]=255;
                }
            }else for(unsigned y=0;y<rows;++y)memcpy(shadow+y*m->pitch,snapshot+source+y*row,row);
            result=cube?c->config.backend.upload_cube_texture(c->config.backend.context,r->handle,face,level,0,0,m->width,m->height,0,m->pitch,shadow):c->config.backend.upload_texture(c->config.backend.context,r->handle,level,0,0,m->width,m->height,0,m->pitch,shadow);
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

static void api_dds_texture(void){dds_texture(0);}
static void api_dds_cube(void){dds_texture(1);}
