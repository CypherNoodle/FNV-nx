#ifndef FNV_D3D9_NORMAL_TRANSFORM_H
#define FNV_D3D9_NORMAL_TRANSFORM_H
#include "d3d9_fixed_vertex.h"
#include <math.h>
/* Bounded affine row-vector normal transform: inverse-transpose of world*view.
 * Output is three column float4 registers; shader normalization is separate.
 * Double intermediates limit cofactor overflow; no arbitrary near-zero cutoff.
 * Reject singular/nonfinite/non-affine matrices unchanged, never use identity.
 * This CPU helper is not connected to a lit GPU shader yet. */
static inline uint32_t fnv_fixed_normal_columns(const float world[16],const float view[16],float out[12]){
    if(!world || !view || !out)return 0x8876086cu;
    for(unsigned i=0;i<16;i++)if(!isfinite(world[i]) || !isfinite(view[i]))return 0x8876086cu;
    if(world[3]!=0 || world[7]!=0 || world[11]!=0 || world[15]!=1 ||
       view[3]!=0 || view[7]!=0 || view[11]!=0 || view[15]!=1)return 0x8876086cu;
    float m[16];fnv_fixed_matrix_product(m,world,view);
    for(unsigned i=0;i<16;i++)if(!isfinite(m[i]))return 0x8876086cu;
    double a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    double cof[9]={e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
    double determinant=a*cof[0]+b*cof[1]+c*cof[2];if(!isfinite(determinant) || determinant==0)return 0x8876086cu;
    float columns[12]={0};for(unsigned col=0;col<3;col++)for(unsigned row=0;row<3;row++){
        double v=cof[row*3+col]/determinant;if(!isfinite(v) || fabs(v)>FLT_MAX)return 0x8876086cu;
        columns[col*4+row]=(float)v;
    }
    memcpy(out,columns,sizeof(columns));return 0;
}
#endif
