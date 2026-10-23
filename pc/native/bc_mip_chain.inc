/* Own bounded BC1/2/3 decode -> straight RGBA box filter -> BC encode.
 * Called before COM allocation/locking. Existing levels remain byte-identical.
 * This is a port filtering policy, not a bit-exact clone of D3DX compression. */
#define stb_compress_dxt_block fnv_stb_compress_dxt_block
#define stb_compress_bc4_block fnv_stb_compress_bc4_block
#define stb_compress_bc5_block fnv_stb_compress_bc5_block
#define STB_DXT_IMPLEMENTATION
#include "../third_party/stb_dxt/stb_dxt.h"
#undef STB_DXT_IMPLEMENTATION
#undef stb_compress_dxt_block
#undef stb_compress_bc4_block
#undef stb_compress_bc5_block
static void fnv_bc_decode(const unsigned char *input,uint32_t format,unsigned char rgba[64]){
    unsigned unit=format==0x31545844?8:16;const unsigned char *color=input+(unit==8?0:8);
    unsigned c0=color[0]|((unsigned)color[1]<<8),c1=color[2]|((unsigned)color[3]<<8),colors[4][3];
    for(unsigned n=0;n<2;++n){unsigned v=n?c1:c0;
        colors[n][0]=((v>>11)*255+15)/31;colors[n][1]=(((v>>5)&63)*255+31)/63;colors[n][2]=((v&31)*255+15)/31;}
    int cutout=unit==8 && c0<=c1;
    for(unsigned c=0;c<3;++c){colors[2][c]=cutout?(colors[0][c]+colors[1][c]+1)/2:(2*colors[0][c]+colors[1][c]+1)/3;
        colors[3][c]=cutout?0:(colors[0][c]+2*colors[1][c]+1)/3;}
    uint32_t select=fnv_dds_u32(color+4);unsigned alpha[8]={input[0],input[1]};uint64_t indices=0;
    if(format==0x35545844){
        if(alpha[0]>alpha[1])for(unsigned i=2;i<8;++i)alpha[i]=((8-i)*alpha[0]+(i-1)*alpha[1])/7;
        else {for(unsigned i=2;i<6;++i)alpha[i]=((6-i)*alpha[0]+(i-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
        for(unsigned i=0;i<6;++i)indices|=(uint64_t)input[2+i]<<(i*8);
    }
    for(unsigned p=0;p<16;++p){unsigned index=(select>>(2*p))&3;
        for(unsigned c=0;c<3;++c)rgba[p*4+c]=(unsigned char)colors[index][c];
        unsigned a=format==0x35545844?alpha[(indices>>(3*p))&7]:format==0x33545844?((input[p/2]>>((p%2)*4))&15)*17:cutout && index==3?0:255;
        rgba[p*4+3]=(unsigned char)a;
    }
}
static void fnv_bc_encode(const unsigned char rgba[64],uint32_t format,unsigned char *output){
    if(format==0x35545844){fnv_stb_compress_dxt_block(output,rgba,1,STB_DXT_NORMAL);return;}
    _Alignas(4) unsigned char opaque[64];memcpy(opaque,rgba,64);unsigned transparent=0,count=0,sums[3]={0};
    for(unsigned p=0;p<16;++p){if(rgba[p*4+3]<128)transparent++;else {count++;for(unsigned c=0;c<3;++c)sums[c]+=rgba[p*4+c];}opaque[p*4+3]=255;}
    if(format==0x33545844){
        memset(output,0,8);for(unsigned p=0;p<16;++p)output[p/2]|=(unsigned char)(((rgba[p*4+3]*15+127)/255)<<((p%2)*4));
        fnv_stb_compress_dxt_block(output+8,opaque,0,STB_DXT_NORMAL);return;
    }
    if(!transparent){fnv_stb_compress_dxt_block(output,opaque,0,STB_DXT_NORMAL);return;}
    /* Fit endpoints without letting invisible RGB values skew the fit. */
    if(!count){memset(output,0,4);memset(output+4,255,4);return;}
    for(unsigned p=0;p<16;++p)if(rgba[p*4+3]<128)for(unsigned c=0;c<3;++c)opaque[p*4+c]=(unsigned char)((sums[c]+count/2)/count);
    _Alignas(4) unsigned char candidate[8];fnv_stb_compress_dxt_block(candidate,opaque,0,STB_DXT_NORMAL);
    unsigned c0=candidate[0]|((unsigned)candidate[1]<<8),c1=candidate[2]|((unsigned)candidate[3]<<8);
    if(c0>c1){unsigned temp=c0;c0=c1;c1=temp;}
    output[0]=(unsigned char)c0;output[1]=(unsigned char)(c0>>8);output[2]=(unsigned char)c1;output[3]=(unsigned char)(c1>>8);
    fnv_dds_put32(output+4,0);
    unsigned colors[3][3];for(unsigned i=0;i<2;++i){unsigned v=i?c1:c0;colors[i][0]=((v>>11)*255+15)/31;colors[i][1]=(((v>>5)&63)*255+31)/63;colors[i][2]=((v&31)*255+15)/31;}
    for(unsigned c=0;c<3;++c)colors[2][c]=(colors[0][c]+colors[1][c]+1)/2;
    uint32_t indices=0;
    for(unsigned p=0;p<16;++p){unsigned best=3;
        if(rgba[p*4+3]>=128){unsigned distance=UINT32_MAX;for(unsigned i=0;i<3;++i){unsigned error=0;for(unsigned c=0;c<3;++c){int delta=(int)rgba[p*4+c]-(int)colors[i][c];error+=(unsigned)(delta*delta);}if(error<distance){distance=error;best=i;}}}
        indices|=best<<(2*p);
    }
    fnv_dds_put32(output+4,indices);
}
static void fnv_rgba_box_half(unsigned char *rgba,unsigned width,unsigned height){
    unsigned w=width>1?width/2:1,h=height>1?height/2:1;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
        unsigned x0=width>1?x*2:0,x1=width>1?x0+1:0,y0=height>1?y*2:0,y1=height>1?y0+1:0;
        for(unsigned c=0;c<4;++c){unsigned sum=rgba[(y0*width+x0)*4+c]+rgba[(y0*width+x1)*4+c]+rgba[(y1*width+x0)*4+c]+rgba[(y1*width+x1)*4+c];
            rgba[(y*w+x)*4+c]=(unsigned char)((sum+2)/4);}
    }
}
static uint32_t fnv_bc_complete_dds(const unsigned char *input,uint32_t bytes,unsigned width,unsigned height,unsigned levels,unsigned maximum,uint32_t format,
    uint32_t shadow_budget,unsigned char **output,uint32_t *output_bytes){
    *output=NULL;*output_bytes=0;
    if(!width || !height || (width&(width-1)) || (height&(height-1)) || width>16384 || height>16384 || !levels || levels>=maximum || maximum>15)return FNV_DDS_UNSUPPORTED;
    unsigned expected=1;for(unsigned n=width>height?width:height;n>1;n>>=1)expected++;
    if(maximum!=expected)return FNV_DDS_UNSUPPORTED;
    if(format!=0x31545844 && format!=0x33545844 && format!=0x35545844)return FNV_DDS_UNSUPPORTED;
    unsigned unit=format==0x31545844?8:16;
    uint64_t supplied=128,total=128;unsigned last_w=width,last_h=height;uint32_t last_offset=128;
    for(unsigned level=0,w=width,h=height;level<maximum;++level){uint64_t size=(uint64_t)((w+3)/4)*((h+3)/4)*unit;
        if(level<levels){last_offset=(uint32_t)supplied;last_w=w;last_h=h;supplied+=size;}
        total+=size;w=w>1?w/2:1;h=h>1?h/2:1;
    }
    uint64_t rgba_bytes=(uint64_t)last_w*last_h*4;
    if(supplied>bytes)return FNV_DDS_INVALID_DATA;
    if(total-128>shadow_budget || total>UINT32_MAX || rgba_bytes>64u*1024*1024)return FNV_D3D9_OUTOFMEMORY;
    unsigned char *dds=malloc((size_t)total),*rgba=malloc((size_t)rgba_bytes);
    if(!dds || !rgba){free(dds);free(rgba);return FNV_D3D9_OUTOFMEMORY;}
    memcpy(dds,input,(size_t)supplied);
    fnv_dds_put32(dds+28,maximum);fnv_dds_put32(dds+8,fnv_dds_u32(dds+8)|0x20000u);fnv_dds_put32(dds+108,fnv_dds_u32(dds+108)|0x400008u);
    _Alignas(4) unsigned char block[64];unsigned source=last_offset;
    for(unsigned by=0;by<(last_h+3)/4;++by)for(unsigned bx=0;bx<(last_w+3)/4;++bx){
        fnv_bc_decode(input+source,format,block);source+=unit;
        for(unsigned y=0;y<4 && by*4+y<last_h;++y)for(unsigned x=0;x<4 && bx*4+x<last_w;++x)
            memcpy(rgba+((by*4+y)*last_w+bx*4+x)*4,block+(y*4+x)*4,4);
    }
    unsigned w=last_w,h=last_h;uint32_t dest=(uint32_t)supplied;
    for(unsigned level=levels;level<maximum;++level){
        fnv_rgba_box_half(rgba,w,h);w=w>1?w/2:1;h=h>1?h/2:1;
        for(unsigned by=0;by<(h+3)/4;++by)for(unsigned bx=0;bx<(w+3)/4;++bx){
            for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){unsigned xx=bx*4+x,yy=by*4+y;if(xx>=w)xx=w-1;if(yy>=h)yy=h-1;
                memcpy(block+(y*4+x)*4,rgba+(yy*w+xx)*4,4);}
            fnv_bc_encode(block,format,dds+dest);dest+=unit;
        }
    }
    free(rgba);if(dest!=(uint32_t)total){free(dds);return FNV_DDS_INVALID_DATA;}
    *output=dds;*output_bytes=dest;return 0;
}
