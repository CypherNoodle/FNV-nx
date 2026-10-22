#ifndef FNV_D3D9_FIXED_VERTEX_H
#define FNV_D3D9_FIXED_VERTEX_H
#include "d3d9_transform_state.h"
/* Row-vector D3D position * world * view * projection. Stage output for alias
 * safety; this helper does not mutate or upload programmable constants. */
static inline void fnv_fixed_matrix_product(float out[16],const float a[16],const float b[16]){
    float p[16];
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++){
        float v=a[r*4]*b[c];for(unsigned k=1;k<4;k++)v=v+a[r*4+k]*b[k*4+c];
        p[r*4+c]=v;
    }
    memcpy(out,p,64);
}
/* Four c-register columns consumed by fixed_unlit.vs.hlsl's dot products.
 * Caller serializes the bank snapshot with COM matrix changes. */
static inline uint32_t fnv_fixed_vertex_columns(const struct fnv_transform_state *s,float out[16]){
    if(!s || !out)return 0x8876086cu;
    float wv[16],wvp[16],columns[16];
    fnv_fixed_matrix_product(wv,s->matrices[10],s->matrices[0]);
    fnv_fixed_matrix_product(wvp,wv,s->matrices[1]);
    for(unsigned c=0;c<4;c++)for(unsigned r=0;r<4;r++)columns[c*4+r]=wvp[r*4+c];
    memcpy(out,columns,64);return 0;
}
#endif
