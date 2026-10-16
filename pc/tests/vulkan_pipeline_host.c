/* Desktop offscreen Vulkan test for our own translated D3D9 shader pair.
 * No window, swapchain or game execution. Exact textured/depth pixel checks.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef FNV_VULKAN_SUBMITTED
#define FNV_VULKAN_SUBMITTED() ((void)0)
#endif
#ifndef FNV_VULKAN_COMPLETED
#define FNV_VULKAN_COMPLETED() ((void)0)
#endif
#ifndef FNV_VULKAN_GEOMETRY
#define FNV_VULKAN_GEOMETRY(i,v,b) ((void)0)
#endif
#ifndef FNV_VULKAN_TEXTURE
#define FNV_VULKAN_TEXTURE(i,t) ((void)0)
#endif

#define FUNCTIONS(X) \
 X(CreateInstance) X(DestroyInstance) X(EnumeratePhysicalDevices) \
 X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceQueueFamilyProperties) \
 X(CreateDevice) X(DestroyDevice) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) \
 X(CreateRenderPass) X(DestroyRenderPass) \
 X(CreateGraphicsPipelines) X(DestroyPipeline) \
 X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceFormatProperties) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
 X(AllocateMemory) X(FreeMemory) X(MapMemory) X(UnmapMemory) \
 X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
 X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) \
 X(CmdCopyBufferToImage) X(CmdCopyImageToBuffer) X(CmdBeginRenderPass) X(CmdEndRenderPass) \
 X(CmdBindPipeline) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) \
 X(CmdBindDescriptorSets) X(CmdDrawIndexed) X(GetDeviceQueue) X(QueueSubmit) \
 X(CreateFence) X(DestroyFence) X(WaitForFences)
#define DECLARE(n) static PFN_vk##n vk##n;
FUNCTIONS(DECLARE)
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"failed line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) do { VkResult r_=(x); if(r_!=VK_SUCCESS){fprintf(stderr,"VkResult %d line %d\n",r_,__LINE__);exit(1);} }while(0)

static VkShaderModule module(VkDevice device, const char *path) {
    FILE *f=fopen(path,"rb"); REQUIRE(f);
    REQUIRE(!fseek(f,0,SEEK_END)); long size=ftell(f); rewind(f);
    REQUIRE(size>=20 && size<=4*1024*1024 && size%4==0);
    uint32_t *code=malloc((size_t)size); REQUIRE(code);
    REQUIRE(fread(code,1,(size_t)size,f)==(size_t)size); REQUIRE(!fclose(f));
    REQUIRE(code[0]==0x07230203);
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=(size_t)size,.pCode=code};
    VkShaderModule result;
    OK(vkCreateShaderModule(device,&ci,NULL,&result));
    free(code); return result;
}

typedef struct Buffer { VkBuffer handle; VkDeviceMemory memory; } Buffer;
typedef struct Image { VkImage handle; VkDeviceMemory memory; VkImageView view; } Image;

static VkDeviceMemory memory(VkDevice d,VkPhysicalDevice p,VkMemoryRequirements req,VkMemoryPropertyFlags flags){
    VkPhysicalDeviceMemoryProperties m;vkGetPhysicalDeviceMemoryProperties(p,&m);
    for(uint32_t i=0;i<m.memoryTypeCount;++i)
        if((req.memoryTypeBits&(1u<<i)) && (m.memoryTypes[i].propertyFlags&flags)==flags){
            VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .allocationSize=req.size,.memoryTypeIndex=i};
            VkDeviceMemory result;OK(vkAllocateMemory(d,&ai,NULL,&result));return result;
        }
    REQUIRE(0);return VK_NULL_HANDLE;
}
static Buffer buffer(VkDevice d,VkPhysicalDevice p,VkDeviceSize size,VkBufferUsageFlags usage,const void *data){
    Buffer b;VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=size,.usage=usage};
    OK(vkCreateBuffer(d,&ci,NULL,&b.handle));
    VkMemoryRequirements req;vkGetBufferMemoryRequirements(d,b.handle,&req);
    b.memory=memory(d,p,req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    OK(vkBindBufferMemory(d,b.handle,b.memory,0));
    if(data){void *mapped;OK(vkMapMemory(d,b.memory,0,size,0,&mapped));memcpy(mapped,data,(size_t)size);vkUnmapMemory(d,b.memory);}
    return b;
}
static void free_buffer(VkDevice d,Buffer b){vkDestroyBuffer(d,b.handle,NULL);vkFreeMemory(d,b.memory,NULL);}
static Image image(VkDevice d,VkPhysicalDevice p,uint32_t width,uint32_t height,VkFormat format,
                   VkImageUsageFlags usage,VkImageAspectFlags aspect){
    Image im;VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
        .format=format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
        .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    OK(vkCreateImage(d,&ci,NULL,&im.handle));
    VkMemoryRequirements req;vkGetImageMemoryRequirements(d,im.handle,&req);
    im.memory=memory(d,p,req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);OK(vkBindImageMemory(d,im.handle,im.memory,0));
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im.handle,
        .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,.subresourceRange={aspect,0,1,0,1}};
    OK(vkCreateImageView(d,&vi,NULL,&im.view));return im;
}
static void free_image(VkDevice d,Image im){vkDestroyImageView(d,im.view,NULL);vkDestroyImage(d,im.handle,NULL);vkFreeMemory(d,im.memory,NULL);}
static void transition(VkCommandBuffer cmd,VkImage im,VkImageLayout old,VkImageLayout next,
                       VkAccessFlags src,VkAccessFlags dst,VkPipelineStageFlags a,VkPipelineStageFlags b){
    VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,
        .oldLayout=old,.newLayout=next,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=im,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(cmd,a,b,0,0,NULL,0,NULL,1,&barrier);
}
static unsigned render_probe(VkDevice d,VkPhysicalDevice p,uint32_t family,VkPipelineLayout layout,
                             const VkDescriptorSetLayout sets[4],VkRenderPass pass,const VkPipeline pipelines[2],const char *uniform_path){
    VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(p,VK_FORMAT_D32_SFLOAT,&fp);
    REQUIRE(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    unsigned char texture_pixels[3][16];
    Buffer upload[3];Image tex[3];
    for(unsigned i=0;i<3;++i){
        unsigned char texels[16]={255,255,255,255, 128,128,128,255, 64,64,64,255, 192,192,192,255};
        FNV_VULKAN_TEXTURE(i,texels);memcpy(texture_pixels[i],texels,16);
        upload[i]=buffer(d,p,sizeof(texels),VK_BUFFER_USAGE_TRANSFER_SRC_BIT,texels);
        tex[i]=image(d,p,2,2,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    }
    Buffer vb[3],ib[3];
    for(unsigned i=0;i<3;++i){
        float vertices[16]={-1,1,0,0, 1,1,1,0, 1,-1,1,1, -1,-1,0,1};
        uint16_t indices[6]={1,2,3,1,3,4}; /* Signed baseVertex=-1. */
        FNV_VULKAN_GEOMETRY(i,vertices,indices);
        vb[i]=buffer(d,p,sizeof(vertices),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,vertices);
        ib[i]=buffer(d,p,sizeof(indices),VK_BUFFER_USAGE_INDEX_BUFFER_BIT,indices);
    }
    Buffer readback=buffer(d,p,4*4096,VK_BUFFER_USAGE_TRANSFER_DST_BIT,NULL);
    Buffer constants[6];
    uint32_t uniform_sizes[2]={16,16};unsigned char *uniform_data=NULL;
    if(uniform_path){
        FILE *f=fopen(uniform_path,"rb");REQUIRE(f);
        unsigned char magic[8];REQUIRE(fread(magic,1,8,f)==8 && !memcmp(magic,"FNVUBO01",8));
        REQUIRE(fread(uniform_sizes,4,2,f)==2);
        for(unsigned j=0;j<2;++j)REQUIRE(uniform_sizes[j] && uniform_sizes[j]<=65536 && uniform_sizes[j]%16==0);
        size_t size=3*((size_t)uniform_sizes[0]+uniform_sizes[1]);
        uniform_data=malloc(size);REQUIRE(uniform_data);
        REQUIRE(fread(uniform_data,1,size,f)==size && fgetc(f)==EOF && !ferror(f));REQUIRE(!fclose(f));
    }
    for(unsigned i=0;i<3;++i){
        float depth[4]={0,0,i==0?.75f:i==1?.25f:.9f,0},tint[4]={0,0,0,1};tint[i]=1;
        const unsigned char *base=uniform_data?uniform_data+i*((size_t)uniform_sizes[0]+uniform_sizes[1]):NULL;
        constants[i*2]=buffer(d,p,uniform_sizes[0],VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,base?(const void*)base:(const void*)depth);
        constants[i*2+1]=buffer(d,p,uniform_sizes[1],VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,base?(const void*)(base+uniform_sizes[0]):(const void*)tint);
    }
    free(uniform_data);
    Image color=image(d,p,32,32,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
    Image depth=image(d,p,32,32,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageView views[2]={color.view,depth.view};
    VkFramebufferCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,
        .attachmentCount=2,.pAttachments=views,.width=32,.height=32,.layers=1};
    VkFramebuffer fb;OK(vkCreateFramebuffer(d,&fci,NULL,&fb));
    VkSamplerCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=0};
    VkSampler sampler;OK(vkCreateSampler(d,&sci,NULL,&sampler));
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,6},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3}};
    VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=9,.poolSizeCount=2,.pPoolSizes=sizes};
    VkDescriptorPool pool;OK(vkCreateDescriptorPool(d,&dpci,NULL,&pool));
    VkDescriptorSet descriptors[3][3];
    for(unsigned i=0;i<3;++i){
        VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool=pool,.descriptorSetCount=3,.pSetLayouts=sets+1};
        OK(vkAllocateDescriptorSets(d,&ai,descriptors[i]));
        VkDescriptorBufferInfo buffers[2]={{constants[i*2].handle,0,uniform_sizes[0]},{constants[i*2+1].handle,0,uniform_sizes[1]}};
        VkDescriptorImageInfo ii={sampler,tex[i].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[3]={
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors[i][0],.descriptorCount=1,
             .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&buffers[0]},
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors[i][1],.descriptorCount=1,
             .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&ii},
            {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors[i][2],.descriptorCount=1,
             .descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&buffers[1]}};
        vkUpdateDescriptorSets(d,3,writes,0,NULL);
    }
    VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool commands;OK(vkCreateCommandPool(d,&cpci,NULL,&commands));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=commands,
        .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;OK(vkAllocateCommandBuffers(d,&cai,&cmd));
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    OK(vkBeginCommandBuffer(cmd,&bi));
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={2,2,1}};
    for(unsigned i=0;i<3;++i){
        transition(cmd,tex[i].handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        vkCmdCopyBufferToImage(cmd,upload[i].handle,tex[i].handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        transition(cmd,tex[i].handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    unsigned draws=0;
    for(unsigned frame=0;frame<4;++frame){
        /* Previous copy and depth writes must finish before discarded attachments are reused. */
        if(frame){
            VkMemoryBarrier m={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0,1,&m,0,NULL,0,NULL);
        }
        VkClearValue clear[2]={ {.color={{0,0,0,1}}}, {.depthStencil={1,0}} };
        VkRenderPassBeginInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=pass,.framebuffer=fb,
            .renderArea={{0,0},{32,32}},.clearValueCount=2,.pClearValues=clear};
        vkCmdBeginRenderPass(cmd,&ri,VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipelines[frame==3]);
        for(unsigned i=frame==3?2:0;i<=(frame==3?2:frame);++i){
            VkDeviceSize zero=0;vkCmdBindVertexBuffers(cmd,0,1,&vb[i].handle,&zero);
            vkCmdBindIndexBuffer(cmd,ib[i].handle,0,VK_INDEX_TYPE_UINT16);
            vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,1,3,descriptors[i],0,NULL);
            vkCmdDrawIndexed(cmd,6,1,0,-1,0);++draws;
        }
        vkCmdEndRenderPass(cmd);
        transition(cmd,color.handle,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        copy=(VkBufferImageCopy){.bufferOffset=frame*4096,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={32,32,1}};
        vkCmdCopyImageToBuffer(cmd,color.handle,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.handle,1,&copy);
    }
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    OK(vkEndCommandBuffer(cmd));
    VkFenceCreateInfo fenceci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;OK(vkCreateFence(d,&fenceci,NULL,&fence));
    VkQueue queue;vkGetDeviceQueue(d,family,0,&queue);
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    OK(vkQueueSubmit(queue,1,&submit,fence));FNV_VULKAN_SUBMITTED();
    OK(vkWaitForFences(d,1,&fence,VK_TRUE,10000000000ull));FNV_VULKAN_COMPLETED();
    void *mapped;OK(vkMapMemory(d,readback.memory,0,4*4096,0,&mapped));
    unsigned pixels=0;
    for(unsigned frame=0;frame<4;++frame)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
        unsigned channel=frame==0?0:frame==3?2:1;
        const unsigned char *actual=(const unsigned char*)mapped+frame*4096+(y*32+x)*4;
        unsigned version=frame==0?0:frame==3?2:1;
        unsigned char expected[4]={0,0,0,255};expected[channel]=texture_pixels[version][((y>=16)*2+(x>=16))*4+channel];
        if(memcmp(actual,expected,4)){
            fprintf(stderr,"pixel frame=%u x=%u y=%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u\n",frame,x,y,
                actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);exit(1);
        }
        ++pixels;
    }
    REQUIRE(draws==7);vkUnmapMemory(d,readback.memory);
    vkDestroyFence(d,fence,NULL);vkDestroyCommandPool(d,commands,NULL);vkDestroyDescriptorPool(d,pool,NULL);
    vkDestroySampler(d,sampler,NULL);vkDestroyFramebuffer(d,fb,NULL);
    for(unsigned i=0;i<3;++i){free_image(d,tex[i]);free_buffer(d,upload[i]);}
    free_image(d,color);free_image(d,depth);
    for(unsigned i=0;i<3;++i){free_buffer(d,vb[i]);free_buffer(d,ib[i]);}
    free_buffer(d,readback);
    for(unsigned i=0;i<6;++i)free_buffer(d,constants[i]);
    return pixels;
}

int main(int argc,char **argv) {
    if(argc!=3 && argc!=4)return 2;
    /* Avoid the current directory / game DLL search path. */
    HMODULE dll=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
    REQUIRE(dll);
#define LOAD(n) do { FARPROC p_=GetProcAddress(dll,"vk" #n); \
    _Static_assert(sizeof(p_)==sizeof(vk##n),"Windows function pointer size"); \
    memcpy(&vk##n,&p_,sizeof(p_)); REQUIRE(vk##n); } while(0);
    FUNCTIONS(LOAD)
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="FNV own shader compiler test",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance; OK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t count=0; OK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    REQUIRE(count>0 && count<=64);
    VkPhysicalDevice physical[64]; OK(vkEnumeratePhysicalDevices(instance,&count,physical));
    VkPhysicalDevice selected=VK_NULL_HANDLE; uint32_t family=0;
    VkPhysicalDeviceProperties properties={0};
    for(uint32_t i=0;i<count && !selected;++i){
        VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(physical[i],&p);
        if(p.apiVersion<VK_API_VERSION_1_1)continue;
        uint32_t n=0; vkGetPhysicalDeviceQueueFamilyProperties(physical[i],&n,NULL);
        REQUIRE(n<=256);
        VkQueueFamilyProperties queues[256];
        vkGetPhysicalDeviceQueueFamilyProperties(physical[i],&n,queues);
        for(uint32_t j=0;j<n;++j)if(queues[j].queueCount && (queues[j].queueFlags&VK_QUEUE_GRAPHICS_BIT)){
            selected=physical[i];family=j;properties=p;break;
        }
    }
    REQUIRE(selected);
    float priority=1;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
    VkDevice device; OK(vkCreateDevice(selected,&dci,NULL,&device));
    VkShaderModule vs=module(device,argv[1]),ps=module(device,argv[2]);
    VkDescriptorSetLayout sets[4];
    for(uint32_t i=0;i<4;++i){
        VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorCount=1,
            .descriptorType=i==2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .stageFlags=i==1?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT};
        VkDescriptorSetLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount=i==0?0:1,.pBindings=i==0?NULL:&binding};
        OK(vkCreateDescriptorSetLayout(device,&ci,NULL,&sets[i]));
    }
    VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=4,.pSetLayouts=sets};
    VkPipelineLayout layout; OK(vkCreatePipelineLayout(device,&lci,NULL,&layout));
    VkAttachmentDescription attachments[2]={
        {.format=VK_FORMAT_R8G8B8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,
         .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
        {.format=VK_FORMAT_D32_SFLOAT,.samples=VK_SAMPLE_COUNT_1_BIT,
         .loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount=1,.pColorAttachments=&color,.pDepthStencilAttachment=&depth};
    VkRenderPassCreateInfo rpci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount=2,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&subpass};
    VkRenderPass pass; OK(vkCreateRenderPass(device,&rpci,NULL,&pass));
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs,.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=ps,.pName="main"}};
    VkVertexInputBindingDescription vb={0,16,VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2]={{0,0,VK_FORMAT_R32G32_SFLOAT,0},{1,0,VK_FORMAT_R32G32_SFLOAT,8}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&vb,.vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=attrs};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={0,32,32,-32,0,1}; VkRect2D scissor={{0,0},{32,32}};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.frontFace=VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL};
    VkPipelineColorBlendAttachmentState ba={.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount=1,.pAttachments=&ba};
    VkGraphicsPipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&ds,
        .pColorBlendState=&bs,.layout=layout,.renderPass=pass};
    VkPipeline pipelines[2];
    OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipelines[0]));
    ds.depthTestEnable=ds.depthWriteEnable=VK_FALSE;
    OK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipelines[1]));
    unsigned pixels=render_probe(device,selected,family,layout,sets,pass,pipelines,argc==4?argv[3]:NULL);
    for(int i=0;i<2;++i)vkDestroyPipeline(device,pipelines[i],NULL);
    vkDestroyRenderPass(device,pass,NULL);vkDestroyPipelineLayout(device,layout,NULL);
    for(int i=0;i<4;++i)vkDestroyDescriptorSetLayout(device,sets[i],NULL);
    vkDestroyShaderModule(device,vs,NULL);vkDestroyShaderModule(device,ps,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);FreeLibrary(dll);
    printf("{\"pipelines_created\":2,\"vendor_id\":%u,\"device_id\":%u,"
           "\"driver_version\":%u,\"api_version\":%u,\"gpu_rendered\":true,\"pixels_checked\":%u,\"draws\":7,"
           "\"game_executed\":false,\"switch_hardware_tested\":false,\"reflected_uniform_data\":%s}\n",
           properties.vendorID,properties.deviceID,properties.driverVersion,properties.apiVersion,pixels,argc==4?"true":"false");
    return 0;
}
