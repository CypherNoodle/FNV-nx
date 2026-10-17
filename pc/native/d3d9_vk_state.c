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

static int blend_factor(uint32_t value,VkBlendFactor *out){
    static const VkBlendFactor factors[11]={VK_BLEND_FACTOR_ZERO,VK_BLEND_FACTOR_ONE,VK_BLEND_FACTOR_SRC_COLOR,VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
        VK_BLEND_FACTOR_SRC_ALPHA,VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,VK_BLEND_FACTOR_DST_ALPHA,VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
        VK_BLEND_FACTOR_DST_COLOR,VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    if(value>=1 && value<=11){*out=factors[value-1];return 1;}
    if(value==14 || value==15){*out=value==14?VK_BLEND_FACTOR_CONSTANT_COLOR:VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;return 1;}
    return 0;
}
uint32_t fnv_d3d9_vk_blend(const struct fnv_d3d9_blend *s,VkPipelineColorBlendAttachmentState *out,float constants[4]){
    if(!s || !out || !constants || s->operation<1 || s->operation>5 || s->operation_alpha<1 || s->operation_alpha>5 || s->write_mask>15)return INVALID;
    VkBlendFactor source,dest,source_alpha,dest_alpha;
    if(!blend_factor(s->source,&source) || !blend_factor(s->destination,&dest) || !blend_factor(s->source_alpha,&source_alpha) || !blend_factor(s->destination_alpha,&dest_alpha))return INVALID;
    VkPipelineColorBlendAttachmentState next={.blendEnable=s->enable!=0,.srcColorBlendFactor=source,.dstColorBlendFactor=dest,.colorBlendOp=(VkBlendOp)(s->operation-1),
        .srcAlphaBlendFactor=s->separate_alpha?source_alpha:source,.dstAlphaBlendFactor=s->separate_alpha?dest_alpha:dest,
        .alphaBlendOp=(VkBlendOp)((s->separate_alpha?s->operation_alpha:s->operation)-1),.colorWriteMask=s->write_mask};
    const float color[4]={((s->constant>>16)&255)/255.f,((s->constant>>8)&255)/255.f,(s->constant&255)/255.f,(s->constant>>24)/255.f};
    *out=next;memcpy(constants,color,sizeof(color));return 0;
}
