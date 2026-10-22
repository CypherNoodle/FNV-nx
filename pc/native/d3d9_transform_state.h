#ifndef FNV_D3D9_TRANSFORM_STATE_H
#define FNV_D3D9_TRANSFORM_STATE_H
#include <stdint.h>
#include <string.h>
#include <float.h>
/* CPU state only. Externally serialize access. No GPU or COM support implied.
 * Binary32, row-major D3DMATRIX storage; compile without FP contraction. */
_Static_assert(sizeof(float)==4,"D3DMATRIX requires binary32 storage");
_Static_assert(FLT_RADIX==2 && FLT_MANT_DIG==24 && FLT_MAX_EXP==128,"binary32 arithmetic required");
struct fnv_transform_state {float matrices[266][16];};
static inline int fnv_transform_index(uint32_t state){
    if(state==2 || state==3)return (int)state-2;
    if(state>=16 && state<=23)return (int)state-14;
    if(state>=256 && state<=511)return (int)state-246;
    return -1;
}
static inline void fnv_transform_init(struct fnv_transform_state *s){
    memset(s,0,sizeof(*s));
    for(unsigned i=0;i<266;i++)for(unsigned j=0;j<4;j++)s->matrices[i][j*5]=1.0f;
}
static inline uint32_t fnv_transform_get(const struct fnv_transform_state *s,uint32_t state,float out[16]){
    int i=fnv_transform_index(state);if(!s || !out || i<0)return 0x8876086cu;
    memmove(out,s->matrices[i],64);return 0;
}
static inline uint32_t fnv_transform_set(struct fnv_transform_state *s,uint32_t state,const float in[16]){
    int i=fnv_transform_index(state);if(!s || !in || i<0)return 0x8876086cu;
    memmove(s->matrices[i],in,64);return 0;
}
static inline uint32_t fnv_transform_multiply(struct fnv_transform_state *s,uint32_t state,const float in[16]){
    int i=fnv_transform_index(state);if(!s || !in || i<0)return 0x8876086cu;
    /* D3D9 MultiplyTransform order is input * current, not current * input.
     * Stage all output so the input may alias any stored matrix. */
    float product[16];const float *current=s->matrices[i];
    for(unsigned r=0;r<4;r++)for(unsigned c=0;c<4;c++){
        float v=in[r*4]*current[c];
        for(unsigned k=1;k<4;k++)v=v+in[r*4+k]*current[k*4+c];
        product[r*4+c]=v;
    }
    memcpy(s->matrices[i],product,64);return 0;
}
#endif
