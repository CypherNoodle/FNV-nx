#include "d3d9_vk_backend.h"
#include "d3d9_vk_prepared.h"
#include "d3d9_fvf.h"
#include "host_mutex.h"
#include "host_clock.h"
#include "d3d9_texture_upload.h"
#include "d3d9_geometry.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define GLOBALS(X) X(CreateInstance)
#define WSI_INSTANCE(X) X(DestroySurfaceKHR) X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) X(GetPhysicalDeviceSurfaceFormatsKHR) X(GetPhysicalDeviceSurfacePresentModesKHR) X(GetPhysicalDeviceFeatures2)
#define WSI_DEVICE(X) X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) X(AcquireNextImageKHR) X(QueuePresentKHR) X(CreateSemaphore) X(DestroySemaphore) X(CmdCopyImage)
#define INSTANCE(X) X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties) \
 X(GetPhysicalDeviceFeatures) X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(GetPhysicalDeviceImageFormatProperties) X(GetPhysicalDeviceFormatProperties) X(CreateDevice) X(GetDeviceProcAddr)
#define DEVICE(X) X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) X(CreateImage) X(DestroyImage) \
 X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) X(CreateImageView) X(DestroyImageView) \
 X(CreateCommandPool) X(DestroyCommandPool) X(ResetCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
 X(CreateFence) X(DestroyFence) X(ResetFences) X(GetFenceStatus) X(WaitForFences) X(QueueSubmit) \
 X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CmdPipelineBarrier) \
 X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdClearAttachments) X(CreateBuffer) X(DestroyBuffer) \
 X(GetBufferMemoryRequirements) X(BindBufferMemory) X(CmdCopyImageToBuffer) X(CmdCopyBufferToImage) X(CmdCopyBuffer) X(MapMemory) X(UnmapMemory) X(InvalidateMappedMemoryRanges) X(FlushMappedMemoryRanges) X(CreateShaderModule) X(DestroyShaderModule) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) X(DestroyPipelineLayout) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CreateSampler) X(DestroySampler) \
 X(CreateGraphicsPipelines) X(DestroyPipeline) X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdBindVertexBuffers) X(CmdBindIndexBuffer) X(CmdDraw) X(CmdDrawIndexed)
#define FIELD(n) PFN_vk##n n;
struct dispatch { GLOBALS(FIELD) INSTANCE(FIELD) DEVICE(FIELD) WSI_INSTANCE(FIELD) WSI_DEVICE(FIELD) };
#undef FIELD
struct surface {
    uintptr_t id;uint32_t refs,pins,width,height,depth,flag;
    VkImage image;VkDeviceMemory memory;VkImageView view;VkDeviceSize allocation;
    VkImageLayout layout;
    struct texture_version *texture;uintptr_t parent_texture;
};
struct buffer_version {VkBuffer buffer;VkDeviceMemory memory;VkDeviceSize allocation;uint32_t refs;};
struct buffer_resource {
    uintptr_t id;uint32_t refs,pins,kind,bytes,usage,format,pool;
    unsigned char *shadow;struct buffer_version *version;
};
struct texture_version {VkImage image;VkImageView view;VkDeviceMemory memory;VkDeviceSize allocation;uint32_t refs,initialized;};
struct texture_resource {
    uintptr_t id;uint32_t refs,pins,block_pins,width,height,levels,usage,format,pool;
    size_t bytes,offsets[16];unsigned char *shadow;struct texture_version *version;uint32_t dirty;
};
struct shader_catalog {
    struct shader_catalog *next;enum fnv_d3d9_stage stage;uint32_t code_bytes,spirv_bytes;
    unsigned char *code;uint32_t *spirv;struct fnv_uniform_plan plan;struct fnv_uniform_entry *entries;VkShaderModule variant_module;
};
struct shader_resource {uintptr_t id;uint32_t refs,pins;struct shader_catalog *entry;VkShaderModule module;};
struct declaration_resource {uintptr_t id;uint32_t refs,pins,count;unsigned char elements[65*8];VkFormat formats[64];};
struct program_entry {struct program_entry *next;struct shader_catalog *stages[2];uint32_t count,mask;struct fnv_vk_vertex_input inputs[32];};
struct draw_resources {
    uintptr_t ids[40];uint32_t count;
    struct buffer_version *vertices[16],*indices,*ubo[2];struct texture_version *textures[16];
    VkPipeline pipeline;VkPipelineLayout layout;VkDescriptorSetLayout sets[4];VkDescriptorPool descriptors;VkSampler samplers[16];
};
struct stream_binding {uintptr_t id;uint32_t offset,stride;};
struct clear_command {
    uintptr_t color,depth;uint32_t flags,argb,z,stencil,count;
    VkClearRect rects[32];
};
struct frame {
    VkCommandPool pool;VkCommandBuffer cmd;VkFence fence;int pending;VkSemaphore acquired;VkFence acquire_fence;int acquire_pending;
    uint32_t count;struct clear_command *commands;
    VkRenderPass *passes;VkFramebuffer *buffers;
    uintptr_t read_surface;VkBuffer read_buffer;VkDeviceMemory read_memory;
    VkDeviceSize read_allocation;uint32_t read_coherent;
    struct draw_resources *draw;struct buffer_version *read_version;struct texture_version *read_texture;
};
struct wsi_image {VkImage image;VkSemaphore ready;VkFence presented;int pending;};
struct wsi_state {VkSurfaceKHR surface;VkSwapchainKHR chain;uint32_t count;struct wsi_image images[8];VkPresentModeKHR mode;uint64_t presents,timeouts;};
struct fnv_vk_backend {
    fnv_host_mutex guard;struct dispatch v;struct fnv_vk_config config;
    VkInstance instance;VkPhysicalDevice physical;VkDevice device;VkQueue queue;struct wsi_state wsi;
    VkPhysicalDeviceProperties properties;VkPhysicalDeviceMemoryProperties memory;
    uint32_t bc;uint32_t family;struct surface *surfaces;struct frame frames[3];
    struct buffer_resource *buffer_resources;struct stream_binding streams[16];uintptr_t indices;
    struct program_entry *programs;uint32_t program_count;uint64_t draws,resets;
    struct fnv_d3d9_blend blend_state;struct fnv_d3d9_depth depth_state;struct fnv_d3d9_sampler sampler_state[16];uint32_t cull_mode;
    struct shader_catalog *catalog;uint32_t catalog_count;
    struct shader_resource *shaders;struct declaration_resource *declarations;
    uintptr_t bound_shaders[2],declaration,fixed_vertex,fixed_directional,fixed_pixel[4];uint32_t texture_stage[2][6],texture_coord_index[2];uint32_t lighting,normalize_normals,color_vertex,specular,ambient;struct fnv_uniform_state *uniforms;
    uint32_t buffer_versions,texture_versions;struct texture_resource *textures;uintptr_t texture_bindings[16];
    struct clear_command *commands;uint32_t count;
    uintptr_t next_id,back,auto_depth,color,depth;uint32_t presentation_format;
    uint32_t viewport[6],scissor[4];uint64_t device_refs,allocated,submissions,completions,draw_timeout_ns,draw_waits;
    VkResult last;int lost,in_scene,queue_touched;uint32_t scissor_enabled;
};
static uint32_t result(struct fnv_vk_backend *d,VkResult r){
    d->last=r;
    if(r==VK_SUCCESS)return 0;
    if(r==VK_NOT_READY || r==VK_TIMEOUT)return FNV_VK_BUSY;
    if(r==VK_ERROR_OUT_OF_HOST_MEMORY || r==VK_ERROR_OUT_OF_DEVICE_MEMORY)return FNV_VK_OOM;
    d->lost=1;return FNV_VK_LOST;
}
static struct surface *find(struct fnv_vk_backend *d,uintptr_t id){
    if(id)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->surfaces[i].id==id)return d->surfaces+i;
    return NULL;
}
static struct buffer_resource *find_buffer(struct fnv_vk_backend *d,uintptr_t id){
    if(id && d->buffer_resources)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->buffer_resources[i].id==id)return d->buffer_resources+i;
    return NULL;
}
static struct texture_resource *find_texture(struct fnv_vk_backend *d,uintptr_t id){
    if(id && d->textures)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->textures[i].id==id)return d->textures+i;
    return NULL;
}
static void release_texture_version(struct fnv_vk_backend *d,struct texture_version *v){
    if(v && !--v->refs){d->v.DestroyImageView(d->device,v->view,NULL);d->v.DestroyImage(d->device,v->image,NULL);
        d->v.FreeMemory(d->device,v->memory,NULL);d->allocated-=v->allocation;d->texture_versions--;free(v);}
}
static void collect_texture(struct fnv_vk_backend *d,struct texture_resource *t){
    if(!t || !t->id || t->refs || t->pins)return;
    release_texture_version(d,t->version);free(t->shadow);memset(t,0,sizeof(*t));
}
static struct shader_resource *find_shader(struct fnv_vk_backend *d,uintptr_t id){
    if(id && d->shaders)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->shaders[i].id==id)return d->shaders+i;
    return NULL;
}
static struct declaration_resource *find_declaration(struct fnv_vk_backend *d,uintptr_t id){
    if(id && d->declarations)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->declarations[i].id==id)return d->declarations+i;
    return NULL;
}
static void collect_shader(struct fnv_vk_backend *d,struct shader_resource *s){
    if(!s || !s->id || s->refs || s->pins)return;
    d->v.DestroyShaderModule(d->device,s->module,NULL);memset(s,0,sizeof(*s));
}
static void collect_declaration(struct declaration_resource *v){
    if(v && v->id && !v->refs && !v->pins)memset(v,0,sizeof(*v));
}
static uint32_t resource_count(struct fnv_vk_backend *d){
    uint32_t n=0;for(uint32_t i=0;i<d->config.resource_capacity;++i)n+=(d->surfaces[i].id!=0)+(d->buffer_resources[i].id!=0)+(d->textures[i].id!=0)+(d->shaders[i].id!=0)+(d->declarations[i].id!=0);return n;
}
static void release_version(struct fnv_vk_backend *d,struct buffer_version *v){
    if(v && !--v->refs){d->v.DestroyBuffer(d->device,v->buffer,NULL);d->v.FreeMemory(d->device,v->memory,NULL);
        d->allocated-=v->allocation;d->buffer_versions--;free(v);}
}
static void collect_buffer(struct fnv_vk_backend *d,struct buffer_resource *b){
    if(!b || !b->id || b->refs || b->pins)return;
    release_version(d,b->version);free(b->shadow);memset(b,0,sizeof(*b));
}
static void pin(struct fnv_vk_backend *,uintptr_t,int);
static void collect(struct fnv_vk_backend *d,struct surface *s){
    if(!s || !s->id || s->refs || s->pins)return;
    if(s->texture){struct texture_version *v=s->texture;uintptr_t parent=s->parent_texture;memset(s,0,sizeof(*s));release_texture_version(d,v);pin(d,parent,0);return;}
    d->v.DestroyImageView(d->device,s->view,NULL);d->v.DestroyImage(d->device,s->image,NULL);
    d->v.FreeMemory(d->device,s->memory,NULL);d->allocated-=s->allocation;memset(s,0,sizeof(*s));
}
static void pin(struct fnv_vk_backend *d,uintptr_t id,int add){
    struct surface *s=find(d,id);
    if(s){if(add)s->pins++;else if(s->pins){s->pins--;collect(d,s);}return;}
    struct buffer_resource *b=find_buffer(d,id);
    if(b){if(add)b->pins++;else if(b->pins){b->pins--;collect_buffer(d,b);}return;}
    struct texture_resource *t=find_texture(d,id);
    if(t){if(add)t->pins++;else if(t->pins){t->pins--;collect_texture(d,t);}return;}
    struct shader_resource *shader=find_shader(d,id);
    if(shader){if(add)shader->pins++;else if(shader->pins){shader->pins--;collect_shader(d,shader);}return;}
    struct declaration_resource *decl=find_declaration(d,id);
    if(decl){if(add)decl->pins++;else if(decl->pins){decl->pins--;collect_declaration(decl);}}
}
static void close_device(struct fnv_vk_backend *d){
    uintptr_t fixed=d->fixed_vertex;d->fixed_vertex=0;pin(d,fixed,0);
    fixed=d->fixed_directional;d->fixed_directional=0;pin(d,fixed,0);
    for(unsigned i=0;i<4;i++){fixed=d->fixed_pixel[i];d->fixed_pixel[i]=0;pin(d,fixed,0);}
    uintptr_t ids[4]={d->color,d->depth,d->back,d->auto_depth};
    d->color=d->depth=d->back=d->auto_depth=0;
    for(unsigned i=0;i<4;++i)pin(d,ids[i],0);
    for(unsigned i=0;i<16;++i){uintptr_t id=d->streams[i].id;d->streams[i]=(struct stream_binding){0};pin(d,id,0);}
    for(unsigned i=0;i<16;++i){uintptr_t id=d->texture_bindings[i];d->texture_bindings[i]=0;pin(d,id,0);}
    for(unsigned i=0;i<2;++i){uintptr_t id=d->bound_shaders[i];d->bound_shaders[i]=0;pin(d,id,0);
        if(d->uniforms)(void)fnv_uniform_state_bind(d->uniforms,(enum fnv_d3d9_stage)i,NULL);}
    uintptr_t declaration=d->declaration;d->declaration=0;pin(d,declaration,0);
    uintptr_t index=d->indices;d->indices=0;pin(d,index,0);
}
static uint32_t allocate(struct fnv_vk_backend *d,VkMemoryRequirements req,VkMemoryPropertyFlags required,VkDeviceMemory *out,uint32_t *coherent){
    if(req.size>d->config.memory_budget-d->allocated)return FNV_VK_OOM;
    uint32_t chosen=UINT32_MAX;
    for(uint32_t i=0;i<d->memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i)) && (d->memory.memoryTypes[i].propertyFlags&required)==required){
        if(chosen==UINT32_MAX)chosen=i;
        if(d->memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT){chosen=i;break;}
    }
    if(chosen==UINT32_MAX)return FNV_VK_OOM;
    VkMemoryAllocateInfo info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=chosen};
    VkDeviceMemory memory=VK_NULL_HANDLE;uint32_t hr=result(d,d->v.AllocateMemory(d->device,&info,NULL,&memory));
    if(!hr){*out=memory;d->allocated+=req.size;if(coherent)*coherent=(d->memory.memoryTypes[chosen].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;}
    return hr;
}
static uint32_t make_version(struct fnv_vk_backend *d,uint32_t kind,uint32_t bytes,const void *data,struct buffer_version **out){
    struct buffer_version *v=calloc(1,sizeof(*v));if(!v)return FNV_VK_OOM;
    VkDeviceSize padded=((VkDeviceSize)bytes+3)&~(VkDeviceSize)3;
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=padded,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|(kind==FNV_D3D9_VS || kind==FNV_D3D9_PS?VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:kind==FNV_D3D9_VB?VK_BUFFER_USAGE_VERTEX_BUFFER_BIT:VK_BUFFER_USAGE_INDEX_BUFFER_BIT)};
    uint32_t hr=result(d,d->v.CreateBuffer(d->device,&ci,NULL,&v->buffer));
    if(hr){free(v);return hr;}
    VkMemoryRequirements req;uint32_t coherent=0;d->v.GetBufferMemoryRequirements(d->device,v->buffer,&req);
    hr=allocate(d,req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&v->memory,&coherent);
    if(!hr){v->allocation=req.size;hr=result(d,d->v.BindBufferMemory(d->device,v->buffer,v->memory,0));}
    void *mapped=NULL;if(!hr)hr=result(d,d->v.MapMemory(d->device,v->memory,0,VK_WHOLE_SIZE,0,&mapped));
    if(!hr){
        memset(mapped,0,(size_t)padded);memcpy(mapped,data,bytes);
        if(!coherent){VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=v->memory,.offset=0,.size=VK_WHOLE_SIZE};
            hr=result(d,d->v.FlushMappedMemoryRanges(d->device,1,&range));}
        d->v.UnmapMemory(d->device,v->memory);
    }
    if(hr){d->v.DestroyBuffer(d->device,v->buffer,NULL);if(v->memory){d->v.FreeMemory(d->device,v->memory,NULL);d->allocated-=v->allocation;}free(v);return hr;}
    v->refs=1;d->buffer_versions++;*out=v;return 0;
}
static uint32_t create_buffer(void *ctx,enum fnv_d3d9_resource_kind kind,uint32_t bytes,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;
    if(!out || !bytes || (kind!=FNV_D3D9_VB && kind!=FNV_D3D9_IB) || pool>1 || (usage&~(8u|512u)) ||
       ((usage&512) && pool!=0) || (kind==FNV_D3D9_IB && format!=101 && format!=102))return FNV_VK_INVALID;
    if(kind==FNV_D3D9_VB){struct fnv_fvf_layout layout;if(fnv_d3d9_fvf_layout(format,&layout))return FNV_VK_INVALID;}
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_OOM;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(d->next_id==UINTPTR_MAX){hr=FNV_VK_INVALID;goto done;}
    if(bytes>d->config.memory_budget || resource_count(d)==d->config.resource_capacity)goto done;
    struct buffer_resource *b=NULL;for(uint32_t i=0;i<d->config.resource_capacity;++i)if(!d->buffer_resources[i].id){b=d->buffer_resources+i;break;}
    if(!b)goto done;
    unsigned char *shadow=calloc(1,bytes);if(!shadow)goto done;
    struct buffer_version *version=NULL;hr=make_version(d,kind,bytes,shadow,&version);
    if(hr){free(shadow);goto done;}
    *b=(struct buffer_resource){.id=++d->next_id,.refs=1,.kind=kind,.bytes=bytes,.usage=usage,.format=format,.pool=pool,.shadow=shadow,.version=version};
    d->device_refs++;*out=b->id;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t update_buffer(void *ctx,uintptr_t id,uint32_t offset,uint32_t bytes,uint32_t flags,const void *data){
    struct fnv_vk_backend *d=ctx;if(!data || !bytes || (flags&~(0x800u|0x1000u|0x2000u)) || (flags&0x3000u)==0x3000u)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);struct buffer_resource *b=find_buffer(d,id);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(!b || offset>b->bytes || bytes>b->bytes-offset || ((flags&0x3000u) && !(b->usage&512)))goto done;
    unsigned char *candidate=malloc(b->bytes);if(!candidate){hr=FNV_VK_OOM;goto done;}
    if(flags&0x2000u)memset(candidate,0,b->bytes);else memcpy(candidate,b->shadow,b->bytes);
    memcpy(candidate+offset,data,bytes);struct buffer_version *version=NULL;hr=make_version(d,b->kind,b->bytes,candidate,&version);
    if(hr){free(candidate);goto done;}
    struct buffer_version *old=b->version;unsigned char *old_shadow=b->shadow;
    b->version=version;b->shadow=candidate;release_version(d,old);free(old_shadow);
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t bind_stream(void *ctx,uint32_t stream,uintptr_t id,uint32_t offset,uint32_t stride){
    struct fnv_vk_backend *d=ctx;if(stream>=16)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);struct buffer_resource *b=find_buffer(d,id);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if((id && (!b || b->kind!=FNV_D3D9_VB || offset>b->bytes)) || stride>d->properties.limits.maxVertexInputBindingStride)goto done;
    uintptr_t old=d->streams[stream].id;pin(d,id,1);d->streams[stream]=(struct stream_binding){id,offset,stride};pin(d,old,0);hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t bind_indices(void *ctx,uintptr_t id){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);struct buffer_resource *b=find_buffer(d,id);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;
    else if(!id || (b && b->kind==FNV_D3D9_IB)){uintptr_t old=d->indices;pin(d,id,1);d->indices=id;pin(d,old,0);hr=0;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t frequency(void *ctx,uint32_t stream,uint32_t setting){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);
    uint32_t hr=d->lost || !d->device_refs?FNV_VK_LOST:stream<16 && setting==1?0:FNV_VK_INVALID;
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t build_surface(struct fnv_vk_backend *d,int depth,uint32_t w,uint32_t h,uint32_t flag,int public_ref,struct surface *out){
    if(d->lost || !d->device_refs)return FNV_VK_LOST;
    if(!w || !h || w>d->properties.limits.maxImageDimension2D || h>d->properties.limits.maxImageDimension2D || d->next_id==UINTPTR_MAX)return FNV_VK_INVALID;
    VkFormat format=depth?VK_FORMAT_D24_UNORM_S8_UINT:VK_FORMAT_B8G8R8A8_UNORM;
    VkImageUsageFlags usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageFormatProperties caps;
    VkResult r=d->v.GetPhysicalDeviceImageFormatProperties(d->physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&caps);
    if(r==VK_ERROR_FORMAT_NOT_SUPPORTED)return FNV_VK_INVALID;
    if(r)return result(d,r);
    if(w>caps.maxExtent.width || h>caps.maxExtent.height || !(caps.sampleCounts&VK_SAMPLE_COUNT_1_BIT))return FNV_VK_INVALID;
    struct surface next={.width=w,.height=h,.depth=(uint32_t)depth,.flag=flag,.refs=(uint32_t)public_ref,.layout=VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
        .extent={w,h,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage};
    uint32_t hr=result(d,d->v.CreateImage(d->device,&ci,NULL,&next.image));if(hr)return hr;
    VkMemoryRequirements req;d->v.GetImageMemoryRequirements(d->device,next.image,&req);
    hr=allocate(d,req,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&next.memory,NULL);
    if(!hr){next.allocation=req.size;hr=result(d,d->v.BindImageMemory(d->device,next.image,next.memory,0));}
    if(!hr){VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=next.image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
        .subresourceRange={depth?(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        hr=result(d,d->v.CreateImageView(d->device,&vi,NULL,&next.view));}
    if(hr){d->v.DestroyImage(d->device,next.image,NULL);if(next.memory){d->v.FreeMemory(d->device,next.memory,NULL);d->allocated-=next.allocation;}return hr;}
    *out=next;return 0;
}
static uint32_t make_surface(struct fnv_vk_backend *d,int depth,uint32_t w,uint32_t h,uint32_t flag,int public_ref,uintptr_t *out){
    if(d->lost || !d->device_refs)return FNV_VK_LOST;
    struct surface *s=NULL;for(uint32_t i=0;i<d->config.resource_capacity;++i)if(!d->surfaces[i].id){s=d->surfaces+i;break;}
    if(!s || resource_count(d)==d->config.resource_capacity)return FNV_VK_OOM;
    struct surface next;uint32_t hr=build_surface(d,depth,w,h,flag,public_ref,&next);if(hr)return hr;
    next.id=++d->next_id;*s=next;if(public_ref)d->device_refs++;*out=next.id;return 0;
}
static void full_viewport(struct fnv_vk_backend *d,struct surface *s){
    const uint32_t v[6]={0,0,s->width,s->height,0,0x3f800000};memcpy(d->viewport,v,sizeof(v));
}
static void release_draw(struct fnv_vk_backend *,struct draw_resources *);
static void retire_frame(struct fnv_vk_backend *d,struct frame *f){
    VkResult reset=d->v.ResetCommandPool(d->device,f->pool,0);if(reset){(void)result(d,reset);d->lost=1;}
    for(uint32_t i=0;i<f->count;++i){
        if(f->buffers[i])d->v.DestroyFramebuffer(d->device,f->buffers[i],NULL);
        if(f->passes[i])d->v.DestroyRenderPass(d->device,f->passes[i],NULL);
        pin(d,f->commands[i].color,0);pin(d,f->commands[i].depth,0);f->buffers[i]=VK_NULL_HANDLE;f->passes[i]=VK_NULL_HANDLE;
    }
    release_draw(d,f->draw);f->draw=NULL;
    if(f->read_buffer)d->v.DestroyBuffer(d->device,f->read_buffer,NULL);
    if(f->read_memory){d->v.FreeMemory(d->device,f->read_memory,NULL);d->allocated-=f->read_allocation;}
    pin(d,f->read_surface,0);
    release_version(d,f->read_version);f->read_version=NULL;
    release_texture_version(d,f->read_texture);f->read_texture=NULL;
    f->read_buffer=VK_NULL_HANDLE;f->read_memory=VK_NULL_HANDLE;f->read_surface=0;f->read_allocation=0;
    f->count=0;if(f->pending){f->pending=0;d->completions++;}
}
static uint32_t wait_locked(struct fnv_vk_backend *d,uint64_t timeout){
    if(d->lost)return FNV_VK_LOST;
    VkFence fences[3];uint32_t n=0;for(uint32_t i=0;i<d->config.frames;++i)if(d->frames[i].pending)fences[n++]=d->frames[i].fence;
    if(!n)return 0;
    uint32_t hr=result(d,d->v.WaitForFences(d->device,n,fences,VK_TRUE,timeout));if(hr)return hr;
    for(uint32_t i=0;i<d->config.frames;++i)if(d->frames[i].pending)retire_frame(d,d->frames+i);
    return d->lost?FNV_VK_LOST:0;
}
static uint32_t wait_for_frame_locked(struct fnv_vk_backend *d,uint64_t timeout){
    VkFence fences[3];uint32_t count=0;
    for(uint32_t i=0;i<d->config.frames;++i){if(!d->frames[i].pending)return 0;fences[count++]=d->frames[i].fence;}
    uint32_t hr=result(d,d->v.WaitForFences(d->device,count,fences,VK_FALSE,timeout));if(hr)return hr;
    int retired=0;
    for(uint32_t i=0;i<d->config.frames;++i){VkResult r=d->v.GetFenceStatus(d->device,d->frames[i].fence);
        if(r==VK_SUCCESS){retire_frame(d,d->frames+i);retired=1;}else if(r!=VK_NOT_READY)return result(d,r);}
    if(!retired)d->lost=1;
    return d->lost?FNV_VK_LOST:0;
}
static void barrier(struct fnv_vk_backend *d,VkCommandBuffer cmd,struct surface *s){
    int fresh=s->layout==VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=fresh?0:(VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT),
        .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.oldLayout=s->layout,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=s->image,
        .subresourceRange={s->depth?(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT):VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    d->v.CmdPipelineBarrier(cmd,fresh?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
    s->layout=VK_IMAGE_LAYOUT_GENERAL;
}
static uint32_t make_pass(struct fnv_vk_backend *d,struct frame *f,uint32_t index,struct surface *color,struct surface *depth){
    VkAttachmentDescription attachments[2]={
        {.format=VK_FORMAT_B8G8R8A8_UNORM,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL},
        {.format=VK_FORMAT_D24_UNORM_S8_UINT,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL}};
    VkAttachmentReference cr={0,VK_IMAGE_LAYOUT_GENERAL},dr={1,VK_IMAGE_LAYOUT_GENERAL};
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&cr,.pDepthStencilAttachment=depth?&dr:NULL};
    VkRenderPassCreateInfo pci={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=depth?2:1,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub};
    VkRenderPass pass=VK_NULL_HANDLE;uint32_t hr=result(d,d->v.CreateRenderPass(d->device,&pci,NULL,&pass));if(hr)return hr;f->passes[index]=pass;
    VkImageView views[2]={color->view,depth?depth->view:VK_NULL_HANDLE};
    VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=f->passes[index],.attachmentCount=depth?2:1,
        .pAttachments=views,.width=color->width,.height=color->height,.layers=1};
    VkFramebuffer framebuffer=VK_NULL_HANDLE;hr=result(d,d->v.CreateFramebuffer(d->device,&fi,NULL,&framebuffer));if(hr)return hr;f->buffers[index]=framebuffer;
    return 0;
}
static uint32_t record_clear(struct fnv_vk_backend *d,struct frame *f,uint32_t index){
    struct clear_command *c=f->commands+index;struct surface *color=find(d,c->color),*depth=find(d,c->depth);
    barrier(d,f->cmd,color);if(depth)barrier(d,f->cmd,depth);
    uint32_t hr=make_pass(d,f,index,color,depth);if(hr)return hr;
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=f->passes[index],.framebuffer=f->buffers[index],.renderArea={{0,0},{color->width,color->height}}};
    d->v.CmdBeginRenderPass(f->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkClearAttachment clear[2];uint32_t n=0;
    if(c->flags&1){clear[n]=(VkClearAttachment){.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.colorAttachment=0};
        float *rgba=clear[n++].clearValue.color.float32;rgba[0]=((c->argb>>16)&255)/255.0f;rgba[1]=((c->argb>>8)&255)/255.0f;rgba[2]=(c->argb&255)/255.0f;rgba[3]=(c->argb>>24)/255.0f;}
    if(c->flags&6){float z;memcpy(&z,&c->z,4);clear[n]=(VkClearAttachment){.aspectMask=((c->flags&2)?VK_IMAGE_ASPECT_DEPTH_BIT:0)|((c->flags&4)?VK_IMAGE_ASPECT_STENCIL_BIT:0)};
        clear[n++].clearValue.depthStencil=(VkClearDepthStencilValue){z,c->stencil};}
    d->v.CmdClearAttachments(f->cmd,n,clear,c->count,c->rects);d->v.CmdEndRenderPass(f->cmd);return 0;
}
static uint32_t begin_frame(struct fnv_vk_backend *d,struct frame **out){
    if(d->lost)return FNV_VK_LOST;
    struct frame *f=NULL;for(uint32_t i=0;i<d->config.frames;++i)if(!d->frames[i].pending){f=d->frames+i;break;}
    if(!f)return FNV_VK_BUSY;
    VkCommandBufferBeginInfo bi={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    uint32_t hr=result(d,d->v.BeginCommandBuffer(f->cmd,&bi));if(!hr)*out=f;else d->lost=1;return hr;
}
static uint32_t submit_frame(struct fnv_vk_backend *d,struct frame *f){
    uint32_t hr=result(d,d->v.EndCommandBuffer(f->cmd));if(hr)return hr;
    hr=result(d,d->v.ResetFences(d->device,1,&f->fence));if(hr)return hr;
    VkSubmitInfo si={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&f->cmd};
    d->queue_touched=1;hr=result(d,d->v.QueueSubmit(d->queue,1,&si,f->fence));
    if(!hr){f->pending=1;d->submissions++;}else d->lost=1;
    return hr;
}
static uint32_t flush_locked(struct fnv_vk_backend *d){
    if(d->lost)return FNV_VK_LOST;
    if(!d->count)return 0;
    struct frame *f=NULL;uint32_t hr=begin_frame(d,&f);if(hr)return hr;
    f->count=d->count;memcpy(f->commands,d->commands,d->count*sizeof(*d->commands));d->count=0;
    for(uint32_t i=0;i<f->count;++i)if((hr=record_clear(d,f,i))!=0){d->lost=1;return hr;}
    hr=submit_frame(d,f);if(hr)d->lost=1;
    else for(unsigned i=0;i<f->count;i++){struct surface *s=find(d,f->commands[i].color);if(s && s->texture && (f->commands[i].flags&1))s->texture->initialized=1;}
    return hr;
}
#include "d3d9_vk_textures.inc"
#include "d3d9_vk_shaders.inc"
#include "d3d9_vk_draw.inc"
#include "d3d9_vk_present.inc"
#include "d3d9_vk_reset.inc"
static void addref(void *ctx,uintptr_t id){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t *refs=NULL;
    struct surface *s=find(d,id);struct buffer_resource *b=find_buffer(d,id);struct texture_resource *t=find_texture(d,id);
    struct shader_resource *shader=find_shader(d,id);struct declaration_resource *decl=find_declaration(d,id);
    if(s)refs=&s->refs;else if(b)refs=&b->refs;else if(t)refs=&t->refs;else if(shader)refs=&shader->refs;else if(decl)refs=&decl->refs;
    if(refs){if(!*refs)d->device_refs++;(*refs)++;}fnv_host_mutex_unlock(&d->guard);
}
static void release(void *ctx,uintptr_t id){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t *refs=NULL;
    struct surface *s=find(d,id);struct buffer_resource *b=find_buffer(d,id);struct texture_resource *t=find_texture(d,id);
    struct shader_resource *shader=find_shader(d,id);struct declaration_resource *decl=find_declaration(d,id);
    if(s)refs=&s->refs;else if(b)refs=&b->refs;else if(t)refs=&t->refs;else if(shader)refs=&shader->refs;else if(decl)refs=&decl->refs;
    if(refs && *refs){if(!--*refs && !--d->device_refs)close_device(d);
        collect(d,s);collect_buffer(d,b);collect_texture(d,t);collect_shader(d,shader);collect_declaration(decl);}
    fnv_host_mutex_unlock(&d->guard);
}
static void device_addref(void *ctx){struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);d->device_refs++;fnv_host_mutex_unlock(&d->guard);}
static void resource_pin(void *ctx,uintptr_t id,int add){struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);struct texture_resource *t=find_texture(d,id);if(t){if(add)t->block_pins++;else t->block_pins--;}pin(d,id,add);fnv_host_mutex_unlock(&d->guard);}
static void device_release(void *ctx){struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);if(d->device_refs && !--d->device_refs)close_device(d);fnv_host_mutex_unlock(&d->guard);}
static uint32_t create_surface(void *ctx,int depth,uint32_t w,uint32_t h,uint32_t format,uint32_t samples,uint32_t quality,uint32_t flag,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;if(!out || (depth!=0 && depth!=1) || format!=(depth?75u:21u) || samples || quality || (!depth && flag) || (depth && flag>1))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=make_surface(d,depth,w,h,flag,1,out);fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t describe(void *ctx,uintptr_t id,uint32_t values[8]){
    struct fnv_vk_backend *d=ctx;if(!values)return FNV_VK_INVALID;fnv_host_mutex_lock(&d->guard);struct surface *s=find(d,id);uint32_t hr=FNV_VK_INVALID;
    if(s){const uint32_t v[8]={s->depth?75u:s->id==d->back && d->presentation_format?d->presentation_format:21u,1,s->depth?2u:1u,0,0,0,s->width,s->height};memcpy(values,v,32);hr=0;}fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_presentation_format(struct fnv_vk_backend *d,uint32_t format){
    if(!d || (format!=21 && format!=22))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);struct surface *back=find(d,d->back);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;
    else if(d->device_refs==1 && back && !back->refs && !d->draws && !d->submissions && !d->count && !d->in_scene){d->presentation_format=format;hr=0;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t set_target(void *ctx,int depth,uint32_t index,uintptr_t id){
    struct fnv_vk_backend *d=ctx;if(index || (depth!=0 && depth!=1))return FNV_VK_INVALID;fnv_host_mutex_lock(&d->guard);
    struct surface *s=find(d,id),*other=find(d,depth?d->color:d->depth);uint32_t hr=FNV_VK_INVALID;
    if(!d->lost && d->device_refs && ((!id && depth) || (s && s->depth==(uint32_t)depth)) &&
       (!s || !other || (depth?(s->width>=other->width && s->height>=other->height):(other->width>=s->width && other->height>=s->height)))){
        uintptr_t *slot=depth?&d->depth:&d->color;uintptr_t old=*slot;pin(d,id,1);*slot=id;pin(d,old,0);
        if(!depth){full_viewport(d,s);d->scissor[0]=d->scissor[1]=0;d->scissor[2]=s->width;d->scissor[3]=s->height;}hr=0;
    }
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t get_target(void *ctx,int depth,uint32_t index,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;if(index || !out || (depth!=0 && depth!=1))return FNV_VK_INVALID;fnv_host_mutex_lock(&d->guard);
    struct surface *s=find(d,depth?d->depth:d->color);uint32_t hr=0x88760866u;
    if(s && d->device_refs){if(!s->refs)d->device_refs++;s->refs++;*out=s->id;hr=0;}fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t state_locked(struct fnv_vk_backend *d,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get){
    if(!values)return FNV_VK_INVALID;
    if(d->lost || !d->device_refs)return FNV_VK_LOST;
    uint32_t hr=FNV_VK_INVALID;
    int handled=0;hr=draw_state(d,kind,sampler,name,values,get,&handled);
    if(handled)return hr;
    if(kind==FNV_D3D9_VIEWPORT){struct surface *s=find(d,d->color);float min=0,max=0;if(!get){memcpy(&min,values+4,4);memcpy(&max,values+5,4);}
        if(get){memcpy(values,d->viewport,24);hr=0;}
        else if(s && values[2] && values[3] && values[0]<=s->width && values[2]<=s->width-values[0] && values[1]<=s->height && values[3]<=s->height-values[1] && isfinite(min) && isfinite(max) && min>=0 && max<=1 && min<=max){memcpy(d->viewport,values,24);hr=0;}
    }else if(kind==FNV_D3D9_SCISSOR){struct surface *s=find(d,d->color);if(get){memcpy(values,d->scissor,16);hr=0;}
        else if(s && values[0]<=values[2] && values[1]<=values[3] && values[2]<=s->width && values[3]<=s->height){memcpy(d->scissor,values,16);hr=0;}}
    else if(kind==FNV_D3D9_RENDER_STATE && name==174){if(get)values[0]=d->scissor_enabled;else d->scissor_enabled=values[0]!=0;hr=0;}
    return hr;
}
static uint32_t state(void *ctx,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t hr=state_locked(d,kind,sampler,name,values,get);fnv_host_mutex_unlock(&d->guard);return hr;
}
#include "d3d9_vk_state_blocks.inc"
static int64_t signed32(uint32_t v){return v&0x80000000u?(int64_t)v-0x100000000ll:v;}
static uint32_t clear(void *ctx,uint32_t count,const uint32_t *rects,uint32_t flags,uint32_t color,uint32_t z,uint32_t stencil){
    struct fnv_vk_backend *d=ctx;if(count>32 || (count && !rects) || !flags || (flags&~7u))return FNV_VK_INVALID;
    float depth_value;memcpy(&depth_value,&z,4);if((flags&2) && (!isfinite(depth_value) || depth_value<0 || depth_value>1))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=0;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(!d->color || ((flags&6) && !d->depth)){hr=FNV_VK_INVALID;goto done;}
    struct clear_command c={.color=d->color,.depth=d->depth,.flags=flags,.argb=color,.z=z,.stencil=stencil};
    int64_t vx=d->viewport[0],vy=d->viewport[1],right=vx+d->viewport[2],bottom=vy+d->viewport[3];
    if(d->scissor_enabled){
        if(vx<d->scissor[0])vx=d->scissor[0];
        if(vy<d->scissor[1])vy=d->scissor[1];
        if(right>d->scissor[2])right=d->scissor[2];
        if(bottom>d->scissor[3])bottom=d->scissor[3];
    }
    for(uint32_t i=0;i<(count?count:1);++i){
        int64_t x=count?signed32(rects[i*4]):vx,y=count?signed32(rects[i*4+1]):vy;
        int64_t r=count?signed32(rects[i*4+2]):right,b=count?signed32(rects[i*4+3]):bottom;
        if(r<x || b<y){hr=FNV_VK_INVALID;goto done;}
        if(x<vx)x=vx;
        if(y<vy)y=vy;
        if(r>right)r=right;
        if(b>bottom)b=bottom;
        if(r>x && b>y)c.rects[c.count++]=(VkClearRect){{{(int32_t)x,(int32_t)y},{(uint32_t)(r-x),(uint32_t)(b-y)}},0,1};
    }
    if(!c.count)goto done;
    if(d->count==d->config.commands){hr=FNV_VK_BUSY;goto done;}
    pin(d,c.color,1);pin(d,c.depth,1);d->commands[d->count++]=c;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t scene(void *ctx,int begin){struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t hr=0;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;else d->in_scene=begin;fnv_host_mutex_unlock(&d->guard);return hr;}
static uint32_t present_device(void *ctx,uint64_t timeout){return fnv_vk_backend_present_sync(ctx,timeout);}
static uint32_t reset_device(void *ctx,uint64_t timeout){return fnv_vk_backend_reset(ctx,timeout);}
static uint32_t cooperative(void *ctx){struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t hr=d->lost || !d->device_refs?FNV_VK_LOST:0;fnv_host_mutex_unlock(&d->guard);return hr;}
void fnv_vk_backend_callbacks(struct fnv_vk_backend *d,struct fnv_d3d9_shader_backend *out){
    if(!out)return;
    *out=(struct fnv_d3d9_shader_backend){.context=d,.create=create_shader,.bind_shader=bind_shader,.create_declaration=create_declaration,.bind_declaration=bind_declaration,.upload_constants=upload_constants,.addref=addref,.release=release,.device_addref=device_addref,.device_release=device_release,
        .create_texture=create_texture,.upload_texture=upload_texture,.bind_texture=bind_texture,.texture_surface=texture_surface,
        .create_buffer=create_buffer,.update_buffer=update_buffer,.bind_stream=bind_stream,.bind_indices=bind_indices,.stream_frequency=frequency,
        .create_surface=create_surface,.surface_desc=describe,.set_target=set_target,.get_target=get_target,.state=state,.state_batch=state_batch,.resource_pin=resource_pin,.clear=clear,.scene=scene,.draw=draw,.draw_fixed_unlit=draw_fixed_unlit,.draw_fixed_directional=draw_fixed_directional,.draw_up=draw_up,.present=d && d->wsi.chain?present_device:NULL,.reset=reset_device,.cooperative=cooperative};
}
uint32_t fnv_vk_backend_adapter_identifier(struct fnv_vk_backend *d,void *out,uint32_t bytes){
    if(!d || !out || bytes!=1104)return FNV_VK_INVALID;
    unsigned char candidate[1104]={0};const char driver[]="FNV-nx Vulkan",display[]="\\\\.\\DISPLAY1";
    fnv_host_mutex_lock(&d->guard);
    memcpy(candidate,driver,sizeof(driver));
    size_t n=0;while(n<sizeof(d->properties.deviceName) && d->properties.deviceName[n])++n;
    memcpy(candidate+512,d->properties.deviceName,n);memcpy(candidate+1024,display,sizeof(display));
    uint32_t vendor=d->properties.vendorID,device=d->properties.deviceID;
    for(unsigned i=0;i<4;++i){candidate[1064+i]=(unsigned char)(vendor>>(i*8));candidate[1068+i]=(unsigned char)(device>>(i*8));}
    memcpy(candidate+1080,d->properties.pipelineCacheUUID,VK_UUID_SIZE);
    fnv_host_mutex_unlock(&d->guard);memcpy(out,candidate,sizeof(candidate));return 0;
}
/* Deliberately limited D3D9 profile: physical limits intersect implemented
 * bridge operations. Offline shader catalogs do not imply generic SM support. */
uint32_t fnv_vk_backend_device_caps(struct fnv_vk_backend *d,void *out,uint32_t bytes){
    if(!d || !out || bytes!=304)return FNV_VK_INVALID;
    unsigned char packed[304];uint32_t words[76]={0};
    fnv_host_mutex_lock(&d->guard);
    if(d->lost || !d->device_refs){fnv_host_mutex_unlock(&d->guard);return FNV_VK_LOST;}
    const VkPhysicalDeviceLimits *l=&d->properties.limits;
    words[0]=1; /* HAL; adapter ordinal is zero. */
    if(d->wsi.chain)words[5]=d->wsi.mode==VK_PRESENT_MODE_FIFO_KHR?1u:0x80000000u;
    words[7]=0x00080000u; /* HW rasterization, not fixed-function HW T&L. */
    words[8]=0x000208f0u; /* culling, color mask, blend op, separate alpha. */
    words[9]=0x01000000u | (d->config.auto_depth?0x10u:0); /* scissor, optional Z. */
    words[10]=d->config.auto_depth?0xffu:0; /* all eight depth comparisons. */
    words[11]=words[12]=0x67ffu; /* mapped blend factors; no dual-source. */
    words[15]=0x00004004u; /* mipmapped 2D textures with alpha; no cube/volume. */
    VkFormatProperties color;d->v.GetPhysicalDeviceFormatProperties(d->physical,VK_FORMAT_R8G8B8A8_UNORM,&color);
    words[16]=0x01010100u; /* point min/mag/mip */
    if(color.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)words[16]|=0x02020200u;
    words[19]=0x17u; /* wrap/mirror/clamp and independent U/V; no arbitrary border. */
    words[22]=words[23]=l->maxImageDimension2D<16384?l->maxImageDimension2D:16384;
    words[25]=1; /* conservative texture-coordinate repetition range */
    words[26]=words[22];words[27]=1; /* aspect ratio, anisotropy */
    words[28]=0x3f800000u;words[44]=0x3f800000u; /* MaxVertexW/point size1 */
    words[45]=65534; /* conservative supported primitive budget, not UINT32_MAX. */
    words[46]=l->maxDrawIndexedIndexValue<0xffffffu?l->maxDrawIndexedIndexValue:0xffffffu;
    words[47]=l->maxVertexInputBindings<16?l->maxVertexInputBindings:16;
    words[48]=l->maxVertexInputBindingStride;
    words[58]=1; /* one adapter in the group */
    words[60]=1; /* one color target, not MRT */
    /* Shader versions/constants, fixed-function stages, lights, stencil,
     * instancing, clip planes, cube/volume and sRGB remain zero. */
    fnv_host_mutex_unlock(&d->guard);
    for(unsigned i=0;i<76;++i)for(unsigned j=0;j<4;++j)packed[i*4+j]=(unsigned char)(words[i]>>(8*j));
    memcpy(out,packed,sizeof(packed));return 0;
}
uint32_t fnv_vk_backend_stats(struct fnv_vk_backend *d,struct fnv_vk_stats *out){
    if(!d || !out)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);
    struct fnv_vk_stats s={.allocated_bytes=d->allocated,.submissions=d->submissions,.completions=d->completions,.device_references=d->device_refs,.queued_commands=d->count,.last_result=d->last};
    for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->surfaces[i].id)s.live_surfaces++;
    for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->buffer_resources[i].id)s.live_buffers++;
    s.shader_catalog_entries=d->catalog_count;s.linked_programs=d->program_count;s.draws=d->draws;s.resets=d->resets;s.draw_waits=d->draw_waits;
    s.presentation_requests=d->wsi.presents;s.acquire_timeouts=d->wsi.timeouts;s.swapchain_images=d->wsi.count;s.present_mode=(uint32_t)d->wsi.mode;
    for(uint32_t i=0;i<d->config.resource_capacity;++i){s.live_shaders+=d->shaders[i].id!=0;s.live_declarations+=d->declarations[i].id!=0;}
    s.buffer_versions=d->buffer_versions;s.texture_versions=d->texture_versions;
    for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->textures[i].id)s.live_textures++;
    for(uint32_t i=0;i<d->config.frames;++i)if(d->frames[i].pending)s.pending_frames++;
    *out=s;fnv_host_mutex_unlock(&d->guard);return 0;
}
uint32_t fnv_vk_backend_draw_wait(struct fnv_vk_backend *d,uint64_t timeout){
    if(!d || timeout==UINT64_MAX)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=0;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;
    else if(d->submissions || d->draws || d->in_scene)hr=FNV_VK_INVALID;
    else d->draw_timeout_ns=timeout;
    fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_flush(struct fnv_vk_backend *d){if(!d)return FNV_VK_INVALID;fnv_host_mutex_lock(&d->guard);uint32_t hr=flush_locked(d);fnv_host_mutex_unlock(&d->guard);return hr;}
uint32_t fnv_vk_backend_wait(struct fnv_vk_backend *d,uint64_t timeout){if(!d)return FNV_VK_INVALID;fnv_host_mutex_lock(&d->guard);uint32_t hr=wait_locked(d,timeout);fnv_host_mutex_unlock(&d->guard);return hr;}
uint32_t fnv_vk_backend_poll(struct fnv_vk_backend *d){
    if(!d)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=d->lost?FNV_VK_LOST:0;
    for(uint32_t i=0;i<d->config.frames && !hr;++i)if(d->frames[i].pending){VkResult r=d->v.GetFenceStatus(d->device,d->frames[i].fence);
        if(r==VK_SUCCESS)retire_frame(d,d->frames+i);else if(r!=VK_NOT_READY)hr=result(d,r);}
    if(d->lost)hr=FNV_VK_LOST;
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t read_resource(struct fnv_vk_backend *d,uintptr_t id,uint32_t offset,uint32_t requested,int buffer_read,void *output,size_t output_bytes,uint64_t timeout){
    if(!d || !output)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);struct surface *s=find(d,id);struct buffer_resource *b=find_buffer(d,id);uint32_t hr=FNV_VK_INVALID;struct texture_resource *t=find_texture(d,id);uint32_t level=offset;
    unsigned char *copy=NULL;struct frame *f=NULL;int held=0;
    if(buffer_read==2){if(!t || level>=t->levels || (!t->version->initialized && !t->dirty) || texture_level_bytes(t,level)>output_bytes)goto done;}
    else if(buffer_read){if(!b || !requested || offset>b->bytes || requested>b->bytes-offset || requested>output_bytes)goto done;}
    else if(!s || s->depth || (uint64_t)s->width*s->height*4>output_bytes)goto done;
    size_t bytes=buffer_read==2?texture_level_bytes(t,level):buffer_read?requested:(size_t)s->width*s->height*4;
    VkDeviceSize aligned=buffer_read==1?offset&~(VkDeviceSize)3:0,skip=buffer_read==1?offset-aligned:0;
    VkDeviceSize transfer_bytes=buffer_read==1?(((VkDeviceSize)offset+bytes+3)&~(VkDeviceSize)3)-aligned:bytes;
    copy=malloc(bytes);if(!copy){hr=FNV_VK_OOM;goto done;}
    pin(d,id,1);held=1;
    if((hr=wait_locked(d,timeout))!=0 || (hr=flush_locked(d))!=0 || (hr=wait_locked(d,timeout))!=0)goto done;
    /* Validate the request first, then drain existing work before a lazy
     * upload needs a command slot. Invalid reads never trigger uploads. */
    if(buffer_read==2 && t->dirty){if((hr=texture_flush(d,t))!=0 || (hr=wait_locked(d,timeout))!=0)goto done;}
    if((hr=begin_frame(d,&f))!=0)goto done;
    f->read_surface=id;pin(d,id,1);
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=transfer_bytes,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkBuffer buffer=VK_NULL_HANDLE;hr=result(d,d->v.CreateBuffer(d->device,&ci,NULL,&buffer));if(!hr)f->read_buffer=buffer;
    if(!hr){VkMemoryRequirements req;d->v.GetBufferMemoryRequirements(d->device,f->read_buffer,&req);
        hr=allocate(d,req,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,&f->read_memory,&f->read_coherent);
        if(!hr){f->read_allocation=req.size;hr=result(d,d->v.BindBufferMemory(d->device,f->read_buffer,f->read_memory,0));}}
    if(hr){retire_frame(d,f);goto done;}
    if(buffer_read==2){
        f->read_texture=t->version;f->read_texture->refs++;
        texture_barrier(d,f->cmd,t->version,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,t->levels);
        VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={mip_size(t->width,level),mip_size(t->height,level),1}};
        d->v.CmdCopyImageToBuffer(f->cmd,t->version->image,VK_IMAGE_LAYOUT_GENERAL,f->read_buffer,1,&region);
    }else if(buffer_read){
        f->read_version=b->version;f->read_version->refs++;
        VkBufferMemoryBarrier source={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=f->read_version->buffer,.offset=0,.size=VK_WHOLE_SIZE};
        d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&source,0,NULL);
        VkBufferCopy region={aligned,0,transfer_bytes};d->v.CmdCopyBuffer(f->cmd,f->read_version->buffer,f->read_buffer,1,&region);
    }else{
        barrier(d,f->cmd,s);
        VkBufferImageCopy region={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={s->width,s->height,1}};
        d->v.CmdCopyImageToBuffer(f->cmd,s->image,VK_IMAGE_LAYOUT_GENERAL,f->read_buffer,1,&region);
    }
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
    if((hr=submit_frame(d,f))!=0){d->lost=1;goto done;}
    if((hr=result(d,d->v.WaitForFences(d->device,1,&f->fence,VK_TRUE,timeout)))!=0)goto done;
    void *mapped=NULL;hr=result(d,d->v.MapMemory(d->device,f->read_memory,0,VK_WHOLE_SIZE,0,&mapped));
    if(!hr){
        if(!f->read_coherent){VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=f->read_memory,.offset=0,.size=VK_WHOLE_SIZE};
            hr=result(d,d->v.InvalidateMappedMemoryRanges(d->device,1,&range));}
        if(!hr)memcpy(copy,(const unsigned char*)mapped+(size_t)skip,bytes);
        d->v.UnmapMemory(d->device,f->read_memory);
    }
    retire_frame(d,f);if(d->lost)hr=FNV_VK_LOST;
    if(!hr)memcpy(output,copy,bytes);
done:
    if(held)pin(d,id,0);
    free(copy);fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_read_color(struct fnv_vk_backend *d,uintptr_t id,void *out,size_t bytes,uint64_t timeout){return read_resource(d,id,0,0,0,out,bytes,timeout);}
uint32_t fnv_vk_backend_read_buffer(struct fnv_vk_backend *d,uintptr_t id,uint32_t offset,uint32_t bytes,void *out,size_t capacity,uint64_t timeout){return read_resource(d,id,offset,bytes,1,out,capacity,timeout);}
uint32_t fnv_vk_backend_read_texture(struct fnv_vk_backend *d,uintptr_t id,uint32_t level,void *out,size_t bytes,uint64_t timeout){return read_resource(d,id,level,0,2,out,bytes,timeout);}
static uint32_t destroy_storage(struct fnv_vk_backend *d){
    if(d->device){
        VkResult idle=VK_SUCCESS;
        if(d->v.DeviceWaitIdle){idle=d->v.DeviceWaitIdle(d->device);
            if(idle!=VK_SUCCESS && idle!=VK_ERROR_DEVICE_LOST && d->queue_touched){d->last=idle;d->lost=1;return FNV_VK_LOST;}}
        uint32_t wsi_hr=destroy_wsi(d,idle==VK_ERROR_DEVICE_LOST);if(wsi_hr)return wsi_hr;
        for(uint32_t i=0;i<d->config.frames;++i){struct frame *f=d->frames+i;
            for(uint32_t j=0;j<d->config.commands;++j){
                if(f->buffers && f->buffers[j])d->v.DestroyFramebuffer(d->device,f->buffers[j],NULL);
                if(f->passes && f->passes[j])d->v.DestroyRenderPass(d->device,f->passes[j],NULL);
            }
            release_draw(d,f->draw);
            if(f->pool)d->v.DestroyCommandPool(d->device,f->pool,NULL);
            if(f->fence)d->v.DestroyFence(d->device,f->fence,NULL);
            if(f->read_buffer)d->v.DestroyBuffer(d->device,f->read_buffer,NULL);
            if(f->read_memory)d->v.FreeMemory(d->device,f->read_memory,NULL);
            release_version(d,f->read_version);release_texture_version(d,f->read_texture);
        }
        if(d->surfaces)for(uint32_t i=0;i<d->config.resource_capacity;++i){struct surface *s=d->surfaces+i;
            if(s->texture)release_texture_version(d,s->texture);
            else {if(s->view)d->v.DestroyImageView(d->device,s->view,NULL);
                if(s->image)d->v.DestroyImage(d->device,s->image,NULL);
                if(s->memory)d->v.FreeMemory(d->device,s->memory,NULL);}
        }
        if(d->buffer_resources)for(uint32_t i=0;i<d->config.resource_capacity;++i){struct buffer_resource *b=d->buffer_resources+i;
            release_version(d,b->version);free(b->shadow);}
        if(d->textures)for(uint32_t i=0;i<d->config.resource_capacity;++i){release_texture_version(d,d->textures[i].version);free(d->textures[i].shadow);}
        if(d->shaders)for(uint32_t i=0;i<d->config.resource_capacity;++i)if(d->shaders[i].module)d->v.DestroyShaderModule(d->device,d->shaders[i].module,NULL);
        for(struct shader_catalog *e=d->catalog;e;e=e->next)if(e->variant_module)d->v.DestroyShaderModule(d->device,e->variant_module,NULL);
        if(d->v.DestroyDevice)d->v.DestroyDevice(d->device,NULL);
    }
    if(d->wsi.surface && d->v.DestroySurfaceKHR)d->v.DestroySurfaceKHR(d->instance,d->wsi.surface,NULL);
    if(d->instance && d->v.DestroyInstance)d->v.DestroyInstance(d->instance,NULL);
    for(uint32_t i=0;i<d->config.frames;++i){free(d->frames[i].commands);free(d->frames[i].passes);free(d->frames[i].buffers);}
    while(d->programs){struct program_entry *entry=d->programs;d->programs=entry->next;free(entry);}
    while(d->catalog){struct shader_catalog *entry=d->catalog;d->catalog=entry->next;free(entry->code);free(entry->spirv);free(entry->entries);free(entry);}
    if(d->uniforms)(void)fnv_uniform_state_destroy(d->uniforms);
    free(d->shaders);free(d->declarations);free(d->surfaces);free(d->buffer_resources);free(d->textures);free(d->commands);fnv_host_mutex_destroy(&d->guard);free(d);return 0;
}
uint32_t fnv_vk_backend_destroy(struct fnv_vk_backend *d){
    if(!d)return FNV_VK_INVALID;
    /* Caller guarantees exclusive ownership for teardown. */
    if(d->device_refs)return FNV_VK_BUSY;
    for(uint32_t i=0;i<d->config.frames;++i)if(d->frames[i].pending && !d->lost)return FNV_VK_BUSY;
    return destroy_storage(d);
}
static uint32_t create_backend(PFN_vkGetInstanceProcAddr gp,const struct fnv_vk_config *cfg,const struct fnv_vk_window *window,struct fnv_vk_backend **out,VkResult *failure){
    _Static_assert(sizeof(uintptr_t)>=8,"Host backend handles require 64-bit host");
    if(failure)*failure=VK_SUCCESS;
    if(!out)return FNV_VK_INVALID;
    *out=NULL;
    if(!gp || !cfg || !cfg->width || !cfg->height || cfg->frames<1 || cfg->frames>3 || cfg->commands<1 || cfg->commands>128 ||
       cfg->resource_capacity<2 || cfg->resource_capacity>256 || !cfg->memory_budget || cfg->auto_depth>1)return FNV_VK_INVALID;
    if(window && (!window->extension || !window->create_surface || window->vsync>1))return FNV_VK_INVALID;
    struct fnv_vk_backend *d=calloc(1,sizeof(*d));if(!d)return FNV_VK_OOM;
    if(!fnv_host_mutex_init(&d->guard)){free(d);return FNV_VK_OOM;}
    d->config=*cfg;d->device_refs=1;uint32_t hr=FNV_VK_LOST;
    d->v.CreateInstance=(PFN_vkCreateInstance)gp(VK_NULL_HANDLE,"vkCreateInstance");if(!d->v.CreateInstance)goto fail;
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="FNV native D3D9 backend",.apiVersion=VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    const char *instance_extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,window?window->extension:NULL};
    if(window){ici.enabledExtensionCount=4;ici.ppEnabledExtensionNames=instance_extensions;}
    if((hr=result(d,d->v.CreateInstance(&ici,NULL,&d->instance)))!=0){d->instance=VK_NULL_HANDLE;goto fail;}
#define LOAD_INSTANCE(n) d->v.n=(PFN_vk##n)gp(d->instance,"vk" #n);if(!d->v.n){hr=FNV_VK_LOST;goto fail;}
    INSTANCE(LOAD_INSTANCE)
    if(window){WSI_INSTANCE(LOAD_INSTANCE)
        VkSurfaceKHR surface=VK_NULL_HANDLE;VkResult r=window->create_surface(window->context,d->instance,gp,&surface);
        if((hr=result(d,r))!=0)goto fail;
        if(!surface){hr=FNV_VK_INVALID;goto fail;}d->wsi.surface=surface;
    }
#undef LOAD_INSTANCE
    uint32_t count=0;
    if((hr=result(d,d->v.EnumeratePhysicalDevices(d->instance,&count,NULL)))!=0)goto fail;
    if(!count || count>64){hr=FNV_VK_INVALID;goto fail;}
    VkPhysicalDevice devices[64];
    if((hr=result(d,d->v.EnumeratePhysicalDevices(d->instance,&count,devices)))!=0)goto fail;
    for(uint32_t i=0;i<count && !d->physical;++i){
        VkPhysicalDeviceProperties props;d->v.GetPhysicalDeviceProperties(devices[i],&props);if(props.apiVersion<VK_API_VERSION_1_1)continue;
        if(window){VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
            VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&maintenance};d->v.GetPhysicalDeviceFeatures2(devices[i],&features);
            if(!maintenance.swapchainMaintenance1)continue;}
        uint32_t n=0;d->v.GetPhysicalDeviceQueueFamilyProperties(devices[i],&n,NULL);if(!n || n>256)continue;
        VkQueueFamilyProperties queues[256];d->v.GetPhysicalDeviceQueueFamilyProperties(devices[i],&n,queues);
        for(uint32_t j=0;j<n;++j)if(queues[j].queueCount && (queues[j].queueFlags&VK_QUEUE_GRAPHICS_BIT)){
            if(window){VkBool32 support=VK_FALSE;if((hr=result(d,d->v.GetPhysicalDeviceSurfaceSupportKHR(devices[i],j,d->wsi.surface,&support)))!=0)goto fail;if(!support)continue;}
            d->physical=devices[i];d->family=j;d->properties=props;break;}
    }
    if(!d->physical){hr=FNV_VK_INVALID;goto fail;}
    d->v.GetPhysicalDeviceMemoryProperties(d->physical,&d->memory);
    VkPhysicalDeviceFeatures supported,enabled={0};d->v.GetPhysicalDeviceFeatures(d->physical,&supported);
    enabled.textureCompressionBC=supported.textureCompressionBC;d->bc=enabled.textureCompressionBC;
    float priority=1;VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=d->family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&enabled};
    const char *device_extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,.swapchainMaintenance1=VK_TRUE};
    if(window){dci.enabledExtensionCount=2;dci.ppEnabledExtensionNames=device_extensions;dci.pNext=&maintenance;}
    if((hr=result(d,d->v.CreateDevice(d->physical,&dci,NULL,&d->device)))!=0){d->device=VK_NULL_HANDLE;goto fail;}
#define LOAD_DEVICE(n) d->v.n=(PFN_vk##n)d->v.GetDeviceProcAddr(d->device,"vk" #n);if(!d->v.n){hr=FNV_VK_LOST;goto fail;}
    DEVICE(LOAD_DEVICE)
    if(window){WSI_DEVICE(LOAD_DEVICE)}
#undef LOAD_DEVICE
    d->v.GetDeviceQueue(d->device,d->family,0,&d->queue);
    uint32_t max_uniform=d->properties.limits.maxUniformBufferRange;if(max_uniform>65536)max_uniform=65536;max_uniform&=~15u;
    struct fnv_uniform_config uc={256,224,cfg->frames*cfg->commands>64?64:cfg->frames*cfg->commands,max_uniform};
    hr=fnv_uniform_state_create(&uc,&d->uniforms);if(hr)goto fail;
    d->blend_state=(struct fnv_d3d9_blend){0,2,1,1,0,2,1,1,15,0xffffffff};
    d->depth_state=(struct fnv_d3d9_depth){1,1,4};d->cull_mode=3;d->lighting=1;d->color_vertex=1;
    const uint32_t initial_stage[2][6]={{4,2,1,2,2,1},{1,2,1,1,2,1}};memcpy(d->texture_stage,initial_stage,sizeof(initial_stage));
    d->texture_coord_index[0]=0;d->texture_coord_index[1]=1;
    for(unsigned i=0;i<16;++i)d->sampler_state[i]=(struct fnv_d3d9_sampler){1,1,1,0,1,1,0,0,0,1};
    d->shaders=calloc(cfg->resource_capacity,sizeof(*d->shaders));d->declarations=calloc(cfg->resource_capacity,sizeof(*d->declarations));
    d->textures=calloc(cfg->resource_capacity,sizeof(*d->textures));
    d->surfaces=calloc(cfg->resource_capacity,sizeof(*d->surfaces));d->buffer_resources=calloc(cfg->resource_capacity,sizeof(*d->buffer_resources));d->commands=calloc(cfg->commands,sizeof(*d->commands));
    if(!d->surfaces || !d->buffer_resources || !d->textures || !d->shaders || !d->declarations || !d->commands){hr=FNV_VK_OOM;goto fail;}
    for(uint32_t i=0;i<cfg->frames;++i){struct frame *f=d->frames+i;
        f->commands=calloc(cfg->commands,sizeof(*f->commands));f->passes=calloc(cfg->commands,sizeof(*f->passes));f->buffers=calloc(cfg->commands,sizeof(*f->buffers));
        if(!f->commands || !f->passes || !f->buffers){hr=FNV_VK_OOM;goto fail;}
        VkCommandPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=d->family};
        if((hr=result(d,d->v.CreateCommandPool(d->device,&pci,NULL,&f->pool)))!=0){f->pool=VK_NULL_HANDLE;goto fail;}
        VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=f->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
        if((hr=result(d,d->v.AllocateCommandBuffers(d->device,&ai,&f->cmd)))!=0)goto fail;
        VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if((hr=result(d,d->v.CreateFence(d->device,&fi,NULL,&f->fence)))!=0){f->fence=VK_NULL_HANDLE;goto fail;}
    }
    if((hr=make_surface(d,0,cfg->width,cfg->height,0,0,&d->back))!=0)goto fail;
    pin(d,d->back,1);d->color=d->back;pin(d,d->color,1);full_viewport(d,find(d,d->color));
    d->scissor[2]=cfg->width;d->scissor[3]=cfg->height;
    if(cfg->auto_depth){if((hr=make_surface(d,1,cfg->width,cfg->height,0,0,&d->auto_depth))!=0)goto fail;
        pin(d,d->auto_depth,1);d->depth=d->auto_depth;pin(d,d->depth,1);}
    if(window && (hr=create_wsi(d,window->vsync))!=0)goto fail;
    *out=d;return 0;
fail:
    if(failure)*failure=d->last;
    (void)destroy_storage(d);return hr;
}
uint32_t fnv_vk_backend_create(PFN_vkGetInstanceProcAddr gp,const struct fnv_vk_config *cfg,struct fnv_vk_backend **out,VkResult *failure){return create_backend(gp,cfg,NULL,out,failure);}
uint32_t fnv_vk_backend_create_window(PFN_vkGetInstanceProcAddr gp,const struct fnv_vk_config *cfg,const struct fnv_vk_window *window,struct fnv_vk_backend **out,VkResult *failure){
    if(!window){if(out)*out=NULL;if(failure)*failure=VK_SUCCESS;return FNV_VK_INVALID;}return create_backend(gp,cfg,window,out,failure);
}

uint32_t fnv_vk_backend_check_format(struct fnv_vk_backend *d,uint32_t usage,uint32_t type,uint32_t format){
    if(!d)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=0x8876086au;VkFormat vk_format=VK_FORMAT_UNDEFINED;VkImageUsageFlags flags=0;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(type==1 && usage==1 && format==21){vk_format=VK_FORMAT_B8G8R8A8_UNORM;flags=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;}
    else if(type==1 && usage==2 && format==75){vk_format=VK_FORMAT_D24_UNORM_S8_UINT;flags=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;}
    else if(type==3 && (usage==1 || usage==(1u|0x20000u)) && format==21){
        vk_format=VK_FORMAT_B8G8R8A8_UNORM;flags=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(usage&0x20000u){VkFormatProperties props;d->v.GetPhysicalDeviceFormatProperties(d->physical,vk_format,&props);if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))goto done;}
    }
    else if(type==3 && !(usage&~(512u|0x20000u)) && (format==21 || format==22 || format==23 || format==28 || format==50 || (d->bc && bc_format(format)))){
        vk_format=bc_format(format)?bc_format(format):VK_FORMAT_R8G8B8A8_UNORM;flags=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(usage&0x20000u){VkFormatProperties props;d->v.GetPhysicalDeviceFormatProperties(d->physical,vk_format,&props);if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))goto done;}
    }
    if(!flags)goto done;
    VkImageFormatProperties caps;VkResult query=d->v.GetPhysicalDeviceImageFormatProperties(d->physical,vk_format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,flags,0,&caps);
    if(query==VK_ERROR_FORMAT_NOT_SUPPORTED)goto done;
    hr=result(d,query);if(!hr && !(caps.sampleCounts&VK_SAMPLE_COUNT_1_BIT))hr=0x8876086au;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
