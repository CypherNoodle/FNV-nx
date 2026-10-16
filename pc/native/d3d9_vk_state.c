#include "d3d9_vk_state.h"
#include <math.h>
#include <string.h>
#define INVALID 0x8876086cu
uint32_t fnv_d3d9_vk_depth(const struct fnv_d3d9_depth *s,VkPipelineDepthStencilStateCreateInfo *out){
    if(!s || !out || s->enable>1 || s->compare<1 || s->compare>8)return INVALID;
    VkPipelineDepthStencilStateCreateInfo result={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=s->enable,.depthWriteEnable=s->write!=0,.depthCompareOp=(VkCompareOp)(s->compare-1),
        .minDepthBounds=0,.maxDepthBounds=1};
    *out=result;return 0;
}
uint32_t fnv_d3d9_vk_sampler(const struct fnv_d3d9_sampler *s,uint32_t levels,float max_lod_bias,VkSamplerCreateInfo *out){
    if(!s || !out || !levels || levels>32 || !isfinite(max_lod_bias) || max_lod_bias<0 ||
       s->address_u<1 || s->address_u>4 || s->address_v<1 || s->address_v>4 || s->address_w<1 || s->address_w>4 ||
       s->mag<1 || s->mag>2 || s->min<1 || s->min>2 || s->mip>2 || s->max_mip || s->max_anisotropy!=1)return INVALID;
    float bias;memcpy(&bias,&s->lod_bias_bits,4);
    if(!isfinite(bias) || bias < -max_lod_bias || bias>max_lod_bias)return INVALID;
    static const VkSamplerAddressMode address[4]={VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER};
    VkBorderColor border=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if(s->address_u==4 || s->address_v==4 || s->address_w==4){
        switch(s->border){case 0:break;case 0xff000000u:border=VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;break;
            case 0xffffffffu:border=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;break;default:return INVALID;}
    }
    VkSamplerCreateInfo result={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter=s->mag==1?VK_FILTER_NEAREST:VK_FILTER_LINEAR,.minFilter=s->min==1?VK_FILTER_NEAREST:VK_FILTER_LINEAR,
        .mipmapMode=s->mip==2?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=address[s->address_u-1],.addressModeV=address[s->address_v-1],.addressModeW=address[s->address_w-1],
        .mipLodBias=bias,.maxAnisotropy=1,.minLod=0,.maxLod=s->mip?(float)(levels-1):0,.borderColor=border};
    *out=result;return 0;
}
