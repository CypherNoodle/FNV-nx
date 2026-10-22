#ifndef FNV_D3D9_DIRECTIONAL_VERTEX_H
#define FNV_D3D9_DIRECTIONAL_VERTEX_H
#include "d3d9_normal_transform.h"
#include "d3d9_light_state.h"
/* One directional light, material colors, no specular/color vertex source.
 * c0..3 WVP, c4..6 normal columns, c7 view light direction+normalize flag,
 * c8 diffuse RGB/alpha, c9 emissive+ambient RGB. No programmable bank mutation. */
static inline uint32_t fnv_fixed_directional_columns(const struct fnv_transform_state *s,const uint32_t material[17],const struct fnv_light_bank *lights,uint32_t ambient,int normalize,float out[40]){
    if(!s || !material || !lights || !out)return 0x8876086cu;
    float data[40]={0},m[17];memcpy(m,material,68);
    for(unsigned i=0;i<17;i++)if(!isfinite(m[i]))return 0x8876086cu;
    uint32_t hr=fnv_fixed_vertex_columns(s,data);if(hr)return hr;
    hr=fnv_fixed_normal_columns(s->matrices[10],s->matrices[0],data+16);if(hr)return hr;
    const struct fnv_light *light=NULL;
    for(unsigned i=0;i<lights->count;i++)if(lights->entries[i].enabled){if(light || lights->entries[i].words[0]!=3)return 0x8876086au;light=lights->entries+i;}
    float l[26]={0};if(light)memcpy(l,light->words,104);
    for(unsigned i=1;i<26;i++)if(!isfinite(l[i]))return 0x8876086cu;
    if(light){
        const float *v=s->matrices[0];double d[3];
        for(unsigned c=0;c<3;c++)d[c]=(double)l[16]*v[c]+(double)l[17]*v[4+c]+(double)l[18]*v[8+c];
        double length=sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);if(!isfinite(length) || !length)return 0x8876086cu;
        for(unsigned c=0;c<3;c++)data[28+c]=(float)(d[c]/length);
    }else data[30]=1;
    data[31]=normalize?1.0f:0.0f;
    for(unsigned c=0;c<3;c++){
        unsigned shift=(2-c)*8;float global=(float)((ambient>>shift)&255)/255.0f;
        data[32+c]=m[c]*l[1+c];data[36+c]=m[12+c]+m[4+c]*(global+l[9+c]);
    }
    data[35]=m[3];
    for(unsigned i=0;i<40;i++)if(!isfinite(data[i]))return 0x8876086cu;
    memcpy(out,data,sizeof(data));return 0;
}
#endif
