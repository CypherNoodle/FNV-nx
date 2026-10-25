/* Framing/shape tests, not a SPIR-V semantic validator. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define FNV_VK_INVALID 0x8876086cu
#define FNV_VK_OOM 0x8007000eu
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
#include "d3d9_vk_sampler_shapes.inc"
int main(void){
 uint32_t a[]={0x07230203,0x10000,0,16,0,(9u<<16)|25,1,15,3,0,0,0,1,0,(3u<<16)|27,2,1,(4u<<16)|32,3,0,2,(4u<<16)|59,3,4,0,(4u<<16)|71,4,34,2,(4u<<16)|71,4,33,0};
 uint32_t mask=0xdeadbeef,copy[sizeof(a)/4];unsigned negatives=0;
 assert(!sampler_shapes(a,sizeof(a),1,&mask)&&mask==1);a[8]=1;assert(!sampler_shapes(a,sizeof(a),1,&mask)&&!mask);a[8]=3;
 const unsigned positions[]={3,5,6,8,9,10,11,12,16,19,22,24,27,29,31,32};
 const uint32_t values[]={65537,0,16,0,1,1,1,0,16,16,16,2,16,3,16,16};
 for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);i++){memcpy(copy,a,sizeof(a));copy[positions[i]]=values[i];mask=0xdeadbeef;uint32_t r=sampler_shapes(copy,sizeof(copy),1,&mask);if(r!=FNV_VK_INVALID || mask!=0xdeadbeef)fprintf(stderr,"case=%u word=%u result=%08x mask=%08x\n",i,positions[i],r,mask);assert(r==FNV_VK_INVALID && mask==0xdeadbeef);negatives++;}
 for(unsigned n=0;n<sizeof(a);n++){mask=0xdeadbeef;assert(sampler_shapes(a,n,1,&mask)==FNV_VK_INVALID&&mask==0xdeadbeef);negatives++;}
 mask=0xdeadbeef;assert(sampler_shapes(a,sizeof(a),3,&mask)==FNV_VK_INVALID&&mask==0xdeadbeef);negatives++;
 printf("{\"status\":\"PASS\",\"sampler_shape_negative_cases\":%u}\n",negatives);return 0;
}
