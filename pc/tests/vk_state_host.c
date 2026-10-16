#include "d3d9_vk_state.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned depth_checks,sampler_checks,rejections;
static void bad_depth(struct fnv_d3d9_depth s){
    VkPipelineDepthStencilStateCreateInfo d;memset(&d,0xa5,sizeof(d));unsigned char old[sizeof(d)];memcpy(old,&d,sizeof(d));
    assert(fnv_d3d9_vk_depth(&s,&d)==0x8876086c && !memcmp(old,&d,sizeof(d)));rejections++;
}
static void bad_sampler(struct fnv_d3d9_sampler s,unsigned levels,float cap){
    VkSamplerCreateInfo d;memset(&d,0xa5,sizeof(d));unsigned char old[sizeof(d)];memcpy(old,&d,sizeof(d));
    assert(fnv_d3d9_vk_sampler(&s,levels,cap,&d)==0x8876086c && !memcmp(old,&d,sizeof(d)));rejections++;
}
int main(void){
    const VkCompareOp comparisons[]={VK_COMPARE_OP_NEVER,VK_COMPARE_OP_LESS,VK_COMPARE_OP_EQUAL,VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_COMPARE_OP_GREATER,VK_COMPARE_OP_NOT_EQUAL,VK_COMPARE_OP_GREATER_OR_EQUAL,VK_COMPARE_OP_ALWAYS};
    for(unsigned test=0;test<2;++test)for(unsigned write=0;write<3;++write)for(unsigned compare=1;compare<=8;++compare){
        struct fnv_d3d9_depth s={test,write?write==1?1:0xffffffffu:0,compare};VkPipelineDepthStencilStateCreateInfo d;
        assert(!fnv_d3d9_vk_depth(&s,&d) && d.sType==VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO && !d.pNext);
        assert(d.depthTestEnable==test && d.depthWriteEnable==(write!=0) && d.depthCompareOp==comparisons[compare-1]);
        assert(!d.stencilTestEnable && !d.depthBoundsTestEnable);depth_checks++;
    }
    const VkSamplerAddressMode address[]={VK_SAMPLER_ADDRESS_MODE_REPEAT,VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER};
    for(unsigned u=1;u<=4;++u)for(unsigned v=1;v<=4;++v)for(unsigned w=1;w<=4;++w)
    for(unsigned mag=1;mag<=2;++mag)for(unsigned min=1;min<=2;++min)for(unsigned mip=0;mip<=2;++mip){
        struct fnv_d3d9_sampler s={u,v,w,0xffffffffu,mag,min,mip,0xbf000000u,0,1};VkSamplerCreateInfo d;
        assert(!fnv_d3d9_vk_sampler(&s,5,4,&d));
        assert(d.addressModeU==address[u-1] && d.addressModeV==address[v-1] && d.addressModeW==address[w-1]);
        assert(d.magFilter==(mag==1?VK_FILTER_NEAREST:VK_FILTER_LINEAR) && d.minFilter==(min==1?VK_FILTER_NEAREST:VK_FILTER_LINEAR));
        assert(d.mipmapMode==(mip==2?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST));
        assert(d.maxLod==(mip?4:0) && d.minLod==0 && d.mipLodBias==-.5f && d.maxAnisotropy==1 && !d.anisotropyEnable);
        assert(!d.compareEnable && !d.unnormalizedCoordinates && !d.pNext && !d.flags);sampler_checks++;
    }
    struct fnv_d3d9_sampler s={4,3,3,0,1,1,0,0,0,1};VkSamplerCreateInfo d;
    unsigned borders[]={0,0xff000000u,0xffffffffu};VkBorderColor bc[]={VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
    for(unsigned i=0;i<3;++i){s.border=borders[i];assert(!fnv_d3d9_vk_sampler(&s,1,0,&d) && d.borderColor==bc[i]);sampler_checks++;}
    s.border=0xff123456;bad_sampler(s,1,4);s.address_u=3;assert(!fnv_d3d9_vk_sampler(&s,1,4,&d));
    for(unsigned i=0;i<10;++i){struct fnv_d3d9_sampler invalid=s;uint32_t fields[10];memcpy(fields,&invalid,sizeof(fields));
        fields[i]=i==8?1:i==9?2:i==7?0x7fc00000u:i==3?0xff123456u:99;
        if(i==3)fields[0]=4;
        memcpy(&invalid,fields,sizeof(fields));bad_sampler(invalid,4,4);
    }
    s.address_u=0;bad_sampler(s,1,4);s.address_u=3;s.mag=0;bad_sampler(s,1,4);s.mag=1;
    bad_sampler(s,0,4);bad_sampler(s,33,4);bad_sampler(s,1,-1);
    uint32_t bits[]={0x7f800000,0xff800000,0x7fc12345,0x40800001,0xc0800001};
    for(unsigned i=0;i<5;++i){s.lod_bias_bits=bits[i];bad_sampler(s,1,4);}
    s.lod_bias_bits=0;float nan;uint32_t nb=0x7fc00000;memcpy(&nan,&nb,4);bad_sampler(s,1,nan);
    bad_depth((struct fnv_d3d9_depth){2,1,4});bad_depth((struct fnv_d3d9_depth){1,1,0});bad_depth((struct fnv_d3d9_depth){1,1,9});
    assert(fnv_d3d9_vk_depth(NULL,NULL)==0x8876086c && fnv_d3d9_vk_sampler(NULL,1,4,NULL)==0x8876086c);
    printf("{\"status\":\"PASS\",\"depth_checks\":%u,\"sampler_checks\":%u,\"rejections\":%u,\"gpu_rendered\":false}\n",depth_checks,sampler_checks,rejections);
}
