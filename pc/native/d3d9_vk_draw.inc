#include "d3d9_vk_sampler_shapes.inc"
/* Initial native draws: immediate submissions, explicit backpressure, no cache. */
static void release_draw(struct fnv_vk_backend *d,struct draw_resources *r){
    if(!r)return;
    if(r->pipeline)d->v.DestroyPipeline(d->device,r->pipeline,NULL);
    if(r->descriptors)d->v.DestroyDescriptorPool(d->device,r->descriptors,NULL);
    if(r->layout)d->v.DestroyPipelineLayout(d->device,r->layout,NULL);
    for(unsigned i=0;i<4;++i)if(r->sets[i])d->v.DestroyDescriptorSetLayout(d->device,r->sets[i],NULL);
    for(unsigned i=0;i<16;++i){if(r->samplers[i])d->v.DestroySampler(d->device,r->samplers[i],NULL);
        release_version(d,r->vertices[i]);release_texture_version(d,r->textures[i]);}
    release_version(d,r->indices);for(unsigned i=0;i<2;++i)release_version(d,r->ubo[i]);
    for(unsigned i=0;i<r->count;++i)pin(d,r->ids[i],0);
    free(r);
}
static void draw_pin(struct fnv_vk_backend *d,struct draw_resources *r,uintptr_t id){if(id){pin(d,id,1);r->ids[r->count++]=id;}}
static uint32_t link_program(struct fnv_vk_backend *d,const void *vertex,uint32_t vertex_bytes,const void *pixel,uint32_t pixel_bytes,const struct fnv_vk_vertex_input *inputs,uint32_t count,uint32_t mask,int reuse,const struct fnv_vk_prepared_program *variant){
    if(!d || !vertex || !pixel || !inputs || !count || count>32 || mask&~65535u)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;struct program_entry next={.count=count,.mask=mask};
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    for(struct shader_catalog *e=d->catalog;e;e=e->next){const void *code=e->stage?pixel:vertex;uint32_t bytes=e->stage?pixel_bytes:vertex_bytes;
        if(e->spirv_bytes && !next.stages[e->stage] && e->code_bytes==bytes && !memcmp(e->code,code,bytes) && (!variant || shader_payload(e,variant->stages[e->stage].spirv,variant->stages[e->stage].spirv_bytes,&variant->stages[e->stage].plan)))next.stages[e->stage]=e;}
    if(!next.stages[0] || !next.stages[1] || count>d->properties.limits.maxVertexInputAttributes)goto done;
    uint32_t shape_hr=sampler_shapes(next.stages[1]->spirv,next.stages[1]->spirv_bytes,mask,&next.cube_mask);if(shape_hr){hr=shape_hr;goto done;}
    uint32_t samplers=0;for(unsigned i=0;i<16;++i)samplers+=(mask>>i)&1;
    if(samplers>d->properties.limits.maxPerStageDescriptorSamplers || samplers>d->properties.limits.maxPerStageDescriptorSampledImages)goto done;
    for(uint32_t i=0;i<count;++i){if(inputs[i].usage>13 || inputs[i].index>15 || inputs[i].location>=d->properties.limits.maxVertexInputAttributes)goto done;
        for(uint32_t j=0;j<i;++j)if(inputs[i].location==inputs[j].location || (inputs[i].usage==inputs[j].usage && inputs[i].index==inputs[j].index))goto done;}
    for(struct program_entry *e=d->programs;e;e=e->next)if(same_shader_code(e->stages[0],next.stages[0]) && same_shader_code(e->stages[1],next.stages[1])){
        if(!reuse || (variant && (e->stages[0]!=next.stages[0] || e->stages[1]!=next.stages[1])) || e->count!=count || e->mask!=mask || e->cube_mask!=next.cube_mask)goto done;
        for(uint32_t i=0;i<count;i++)if(e->inputs[i].usage!=inputs[i].usage || e->inputs[i].index!=inputs[i].index || e->inputs[i].location!=inputs[i].location)goto done;
        hr=0;goto done;
    }
    if(d->program_count>=4096){hr=FNV_VK_OOM;goto done;}
    struct program_entry *entry=malloc(sizeof(*entry));if(!entry){hr=FNV_VK_OOM;goto done;}
    memcpy(next.inputs,inputs,count*sizeof(*inputs));next.next=d->programs;*entry=next;d->programs=entry;d->program_count++;hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_link_program(struct fnv_vk_backend *d,const void *vertex,uint32_t vertex_bytes,const void *pixel,uint32_t pixel_bytes,const struct fnv_vk_vertex_input *inputs,uint32_t count,uint32_t mask){return link_program(d,vertex,vertex_bytes,pixel,pixel_bytes,inputs,count,mask,0,NULL);}
uint32_t fnv_vk_backend_ensure_program(struct fnv_vk_backend *d,const void *vertex,uint32_t vertex_bytes,const void *pixel,uint32_t pixel_bytes,const struct fnv_vk_vertex_input *inputs,uint32_t count,uint32_t mask){return link_program(d,vertex,vertex_bytes,pixel,pixel_bytes,inputs,count,mask,1,NULL);}
uint32_t fnv_vk_backend_prepare_variant(struct fnv_vk_backend *d,const struct fnv_vk_prepared_program *p){
    if(!d || !p || !p->inputs || !p->input_count || p->input_count>32 || p->sampler_mask&~65535u)return FNV_VK_INVALID;
    for(unsigned i=0;i<2;i++)if(!shader_header((enum fnv_d3d9_stage)i,p->stages[i].code,p->stages[i].code_bytes) || !spirv_header((enum fnv_d3d9_stage)i,p->stages[i].spirv,p->stages[i].spirv_bytes) || (p->stages[i].plan.count && !p->stages[i].plan.entries))return FNV_VK_INVALID;
    /* Never replace an existing original pair, even during exclusive init. */
    fnv_host_mutex_lock(&d->guard);
    if(d->lost || !d->device_refs){fnv_host_mutex_unlock(&d->guard);return FNV_VK_LOST;}
    for(struct program_entry *e=d->programs;e;e=e->next){
        int match=1;for(unsigned i=0;i<2;i++)if(e->stages[i]->code_bytes!=p->stages[i].code_bytes || memcmp(e->stages[i]->code,p->stages[i].code,p->stages[i].code_bytes))match=0;
        if(!match)continue;
        uint32_t hr=0;
        if(e->count!=p->input_count || e->mask!=p->sampler_mask)hr=FNV_VK_INVALID;
        for(unsigned i=0;i<2;i++)if(!shader_payload(e->stages[i],p->stages[i].spirv,p->stages[i].spirv_bytes,&p->stages[i].plan))hr=FNV_VK_INVALID;
        if(!hr)for(unsigned i=0;i<e->count;i++)if(e->inputs[i].usage!=p->inputs[i].usage || e->inputs[i].index!=p->inputs[i].index || e->inputs[i].location!=p->inputs[i].location)hr=FNV_VK_INVALID;
        fnv_host_mutex_unlock(&d->guard);return hr;
    }
    fnv_host_mutex_unlock(&d->guard);
    for(unsigned i=0;i<2;i++){
        const struct fnv_vk_prepared_stage *s=p->stages+i;
        uint32_t hr=register_shader(d,(enum fnv_d3d9_stage)i,s->code,s->code_bytes,s->spirv,s->spirv_bytes,&s->plan,2);if(hr)return hr;
    }
    return link_program(d,p->stages[0].code,p->stages[0].code_bytes,p->stages[1].code,p->stages[1].code_bytes,p->inputs,p->input_count,p->sampler_mask,1,p);
}
static uint32_t program_module(struct fnv_vk_backend *d,struct shader_catalog *entry,struct shader_resource *shader,VkShaderModule *out){
    if(entry==shader->entry){*out=shader->module;return 0;}
    if(!entry->variant_module){
        VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=entry->spirv_bytes,.pCode=entry->spirv};
        VkShaderModule module=VK_NULL_HANDLE;uint32_t hr=result(d,d->v.CreateShaderModule(d->device,&ci,NULL,&module));if(hr)return hr;entry->variant_module=module;
    }
    *out=entry->variant_module;return 0;
}
static uint32_t draw_state(struct fnv_vk_backend *d,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get,int *handled){
    *handled=0;
    if(kind==FNV_D3D9_TEXTURE_STAGE_STATE){
        *handled=1;if(d->lost || !d->device_refs)return FNV_VK_LOST;
        if(sampler>=2)return FNV_VK_INVALID;
        if(name==11){if(get)values[0]=d->texture_coord_index[sampler];else d->texture_coord_index[sampler]=values[0];return 0;}
        if(name<1 || name>6)return FNV_VK_INVALID;
        if(get)values[0]=d->texture_stage[sampler][name-1];else d->texture_stage[sampler][name-1]=values[0];return 0;
    }
    if(kind==FNV_D3D9_RENDER_STATE && (name==137 || name==143 || name==141 || name==29 || name==139)){
        *handled=1;if(d->lost || !d->device_refs)return FNV_VK_LOST;
        uint32_t *field=name==137?&d->lighting:name==143?&d->normalize_normals:name==141?&d->color_vertex:name==29?&d->specular:&d->ambient;
        if(get)values[0]=*field;else *field=name==139?values[0]:values[0]!=0;return 0;
    }
    if(kind==FNV_D3D9_RENDER_STATE && (name==7 || name==14 || name==23 || name==22)){
        *handled=1;if(d->lost || !d->device_refs)return FNV_VK_LOST;
        uint32_t *field=name==7?&d->depth_state.enable:name==14?&d->depth_state.write:name==23?&d->depth_state.compare:&d->cull_mode;
        if(get){values[0]=*field;return 0;}
        if(name==22){if(values[0]<1 || values[0]>3)return FNV_VK_INVALID;*field=values[0];return 0;}
        struct fnv_d3d9_depth next=d->depth_state;if(name==7)next.enable=values[0];else if(name==14)next.write=values[0];else next.compare=values[0];
        VkPipelineDepthStencilStateCreateInfo test;uint32_t hr=fnv_d3d9_vk_depth(&next,&test);if(!hr)d->depth_state=next;return hr;
    }
    if(kind==FNV_D3D9_RENDER_STATE){
        struct fnv_d3d9_blend next=d->blend_state;uint32_t *field=NULL;
        switch(name){case 27:field=&next.enable;break;case 19:field=&next.source;break;case 20:field=&next.destination;break;case 171:field=&next.operation;break;
            case 206:field=&next.separate_alpha;break;case 207:field=&next.source_alpha;break;case 208:field=&next.destination_alpha;break;case 209:field=&next.operation_alpha;break;
            case 168:field=&next.write_mask;break;case 193:field=&next.constant;break;default:break;}
        if(field){*handled=1;if(d->lost || !d->device_refs)return FNV_VK_LOST;
            if(get){values[0]=*field;return 0;}*field=values[0];if(name==27 || name==206)*field=*field!=0;
            VkPipelineColorBlendAttachmentState attachment;float constants[4];uint32_t hr=fnv_d3d9_vk_blend(&next,&attachment,constants);if(!hr)d->blend_state=next;return hr;}
    }
    if(kind!=FNV_D3D9_SAMPLER_STATE)return FNV_VK_INVALID;
    *handled=1;if(d->lost || !d->device_refs)return FNV_VK_LOST;if(sampler>=16 || name<1 || name>10)return FNV_VK_INVALID;
    /* The struct follows D3DSAMP_ADDRESSU..MAXANISOTROPY exactly. */
    struct fnv_d3d9_sampler next=d->sampler_state[sampler];uint32_t fields[10];memcpy(fields,&next,sizeof(fields));
    if(get){values[0]=fields[name-1];return 0;}
    fields[name-1]=values[0];memcpy(&next,fields,sizeof(fields));VkSamplerCreateInfo test;
    uint32_t hr=fnv_d3d9_vk_sampler(&next,32,d->properties.limits.maxSamplerLodBias,&test);if(!hr)d->sampler_state[sampler]=next;return hr;
}
static uint32_t draw_descriptors(struct fnv_vk_backend *d,struct draw_resources *r,const struct program_entry *program){
    uint32_t samplers=0;for(unsigned i=0;i<16;++i)samplers+=(program->mask>>i)&1;
    for(uint32_t set=0;set<4;++set){VkDescriptorSetLayoutBinding bindings[16];uint32_t count=0;
        if(set==1 || set==3){unsigned stage=set==3;if(r->ubo[stage])bindings[count++]=(VkDescriptorSetLayoutBinding){0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,stage?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_VERTEX_BIT,NULL};}
        if(set==2)for(unsigned i=0;i<16;++i)if(program->mask&(1u<<i))bindings[count++]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
        VkDescriptorSetLayoutCreateInfo ci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=count,.pBindings=bindings};
        VkDescriptorSetLayout layout=VK_NULL_HANDLE;uint32_t hr=result(d,d->v.CreateDescriptorSetLayout(d->device,&ci,NULL,&layout));if(hr)return hr;r->sets[set]=layout;
    }
    VkPipelineLayoutCreateInfo lci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=4,.pSetLayouts=r->sets};
    VkPipelineLayout layout=VK_NULL_HANDLE;uint32_t hr=result(d,d->v.CreatePipelineLayout(d->device,&lci,NULL,&layout));if(hr)return hr;r->layout=layout;
    VkDescriptorPoolSize sizes[2];uint32_t size_count=0,ubos=(r->ubo[0]!=NULL)+(r->ubo[1]!=NULL);
    if(ubos)sizes[size_count++]=(VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,ubos};
    if(samplers)sizes[size_count++]=(VkDescriptorPoolSize){VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,samplers};
    VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=3,.poolSizeCount=size_count,.pPoolSizes=sizes};
    VkDescriptorPool pool=VK_NULL_HANDLE;hr=result(d,d->v.CreateDescriptorPool(d->device,&pci,NULL,&pool));if(hr)return hr;r->descriptors=pool;
    return 0;
}
struct draw_watch {uint64_t started;int active;};
static uint32_t draw_slot(struct fnv_vk_backend *d,struct draw_watch *watch){
    for(unsigned i=0;i<d->config.frames;i++)if(!d->frames[i].pending)return 0;
    if(!d->draw_timeout_ns)return FNV_VK_BUSY;
    uint64_t now;
    if(!fnv_host_now_ns(&now)){d->lost=1;return FNV_VK_LOST;}
    if(!watch->active){watch->started=now;watch->active=1;}
    if(now<watch->started || now-watch->started>=d->draw_timeout_ns){d->lost=1;return FNV_VK_LOST;}
    d->draw_waits++;
    uint32_t hr=wait_for_frame_locked(d,d->draw_timeout_ns-(now-watch->started));
    if(hr==FNV_VK_BUSY){d->lost=1;return FNV_VK_LOST;}
    return hr;
}
static uint32_t draw_locked(struct fnv_vk_backend *d,int indexed,uint32_t primitive,uint32_t base,uint32_t min_vertex,uint32_t vertices,uint32_t start,uint32_t primitives,struct buffer_resource *up_vertex,struct buffer_resource *up_index,uint32_t up_stride,const float *fixed,uint32_t fixed_bytes){
    if(d->lost || !d->device_refs)return FNV_VK_LOST;
    uint32_t hr=FNV_VK_INVALID;struct frame *f=NULL;struct draw_watch watch={0};struct stream_binding streams[16];memcpy(streams,d->streams,sizeof(streams));
    if(up_vertex){memset(streams,0,sizeof(streams));streams[0].stride=up_stride;}
    int screen=fixed && fixed_bytes==16;
    struct shader_resource *vs=find_shader(d,fixed?(screen?d->fixed_screen[0]:fixed_bytes==160?d->fixed_directional:d->fixed_vertex):d->bound_shaders[0]),*ps=find_shader(d,screen?d->fixed_screen[1]:d->bound_shaders[1]);
    int automatic=fixed && fixed_bytes==160 && !d->bound_shaders[1];
    if(fixed && !screen && !automatic && (d->texture_coord_index[0]!=0 || d->texture_coord_index[1]!=1))return 0x8876086au;
    if(automatic){
        const uint32_t *t=d->texture_stage[0];int mode=-1;
        if(t[0]==2 && t[1]==0 && t[3]==2 && t[4]==0)mode=0;
        if(t[0]==4 && t[1]==2 && t[2]==1 && t[4]==2){
            if(t[3]==4 && t[5]==1)mode=1;else if(t[3]==2)mode=2;
        }
        if(mode>0 && d->texture_coord_index[0]!=0)return 0x8876086au;
        if(d->texture_stage[1][0]!=1){
            const uint32_t *next=d->texture_stage[1];
            if(mode!=1 || next[0]!=4 || next[1]!=2 || next[2]!=1 || next[3]!=4 || next[4]!=2 || next[5]!=1 || d->texture_coord_index[1]!=0)return 0x8876086au;
            mode=3;
        }
        if(mode<0 || !(ps=find_shader(d,d->fixed_pixel[mode])))return 0x8876086au;
    }
    struct declaration_resource *decl=find_declaration(d,d->declaration);struct surface *color=find(d,d->color),*depth=find(d,d->depth);
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(fixed && (d->bound_shaders[0] || (screen?(d->bound_shaders[1] || d->lighting):fixed_bytes==64?d->lighting:(!d->lighting || d->specular || d->color_vertex))))goto done;
    if(!d->in_scene || !vs || !ps || !decl || !color || (primitive<4 || primitive>6) || (d->depth_state.enable && !depth))goto done;
    if(d->viewport[2]>d->properties.limits.maxViewportDimensions[0] || d->viewport[3]>d->properties.limits.maxViewportDimensions[1] ||
       (double)d->viewport[0]+.5<d->properties.limits.viewportBoundsRange[0] || (double)d->viewport[1]+.5<d->properties.limits.viewportBoundsRange[0] ||
       (double)d->viewport[0]+d->viewport[2]+.5>d->properties.limits.viewportBoundsRange[1] || (double)d->viewport[1]+d->viewport[3]+.5>d->properties.limits.viewportBoundsRange[1])goto done;
    struct program_entry *program=d->programs;while(program && (!same_shader_code(program->stages[0],vs->entry) || !same_shader_code(program->stages[1],ps->entry)))program=program->next;
    if(!program || (fixed && (program->stages[0]!=vs->entry || program->stages[1]!=ps->entry)))goto done;
    struct fnv_geometry_input geometry={.elements=decl->elements,.element_count=decl->count,.indexed=(uint32_t)indexed,.primitive=primitive,
        .base_bits=base,.min_vertex=min_vertex,.vertices=vertices,.start=start,.primitives=primitives};
    for(unsigned i=0;i<16;++i){struct buffer_resource *b=up_vertex?(i?NULL:up_vertex):find_buffer(d,streams[i].id);
        geometry.streams[i]=(struct fnv_geometry_stream){b?b->bytes:0,streams[i].offset,streams[i].stride,1};}
    struct buffer_resource *ib=up_vertex?up_index:find_buffer(d,d->indices);
    if(indexed){if(!ib)goto done;geometry.indices=ib->shadow;geometry.index_bytes=ib->bytes;geometry.index_size=ib->format==101?2:4;}
    struct fnv_geometry_access access;hr=fnv_d3d9_geometry_validate(&geometry,&access);if(hr)goto done;
    VkVertexInputAttributeDescription attributes[32];VkVertexInputBindingDescription bindings[16];uint32_t binding_count=0,stream_mask=0;
    for(uint32_t i=0;i<program->count;++i){uint32_t found=decl->count;
        for(uint32_t j=0;j<decl->count-1;++j)if(decl->elements[j*8+6]==program->inputs[i].usage && decl->elements[j*8+7]==program->inputs[i].index){found=j;break;}
        if(found==decl->count){hr=FNV_VK_INVALID;goto done;}
        const unsigned char *e=decl->elements+found*8;uint32_t stream=e[0]|(uint32_t)e[1]<<8,offset=e[2]|(uint32_t)e[3]<<8;
        unsigned type=e[4],alignment=type<4?4:(type==6 || type==7 || (type>=9 && type<=12) || type>=15)?2:1;
        if(((uint64_t)streams[stream].offset+offset)%alignment || streams[stream].stride%alignment){hr=FNV_VK_INVALID;goto done;}
        attributes[i]=(VkVertexInputAttributeDescription){program->inputs[i].location,stream,decl->formats[found],offset};stream_mask|=1u<<stream;
    }
    for(unsigned i=0;i<16;++i)if(stream_mask&(1u<<i))bindings[binding_count++]=(VkVertexInputBindingDescription){i,streams[i].stride,VK_VERTEX_INPUT_RATE_VERTEX};
    VkSamplerCreateInfo sampler_info[16];struct texture_resource *sampled[16]={0};
    for(unsigned i=0;i<16;++i)if(program->mask&(1u<<i)){struct texture_resource *t=find_texture(d,d->texture_bindings[i]);
        if(!d->texture_bindings[i]){
            if(program->cube_mask&(1u<<i)){t=&d->null_cube;
                if(!t->width){for(unsigned face=0;face<6;face++)d->null_cube_texels[face*4+3]=255;t->width=t->height=t->levels=1;t->faces=6;t->format=21;t->bytes=24;t->face_bytes=4;t->shadow=d->null_cube_texels;t->dirty=1;}
            }else{t=&d->null_texture;
                if(!t->width){d->null_texel[3]=255;t->width=t->height=t->levels=1;t->format=21;t->bytes=t->face_bytes=4;t->faces=1;t->shadow=d->null_texel;t->dirty=1;}}
        }
        if(t && ((t->faces==6)!=!!(program->cube_mask&(1u<<i)))){hr=FNV_VK_INVALID;goto done;}
        sampled[i]=t;
        if(t && t->usage==1){
            if(color->parent_texture==t->id){hr=FNV_VK_INVALID;goto done;}
            if(d->count){hr=draw_slot(d,&watch);if(hr)goto done;hr=flush_locked(d);if(hr)goto done;}
        }
        if(t && t->dirty){hr=draw_slot(d,&watch);if(hr)goto done;hr=texture_flush(d,t);if(hr)goto done;}
        if(!t || !t->version->initialized){hr=FNV_VK_INVALID;goto done;}
        hr=fnv_d3d9_vk_sampler(d->sampler_state+i,t->levels,d->properties.limits.maxSamplerLodBias,sampler_info+i);if(hr)goto done;
        if(sampler_info[i].magFilter==VK_FILTER_LINEAR || sampler_info[i].minFilter==VK_FILTER_LINEAR || sampler_info[i].mipmapMode==VK_SAMPLER_MIPMAP_MODE_LINEAR){
            VkFormatProperties props;d->v.GetPhysicalDeviceFormatProperties(d->physical,bc_format(t->format)?bc_format(t->format):t->usage==1?VK_FORMAT_B8G8R8A8_UNORM:VK_FORMAT_R8G8B8A8_UNORM,&props);
            if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)){hr=FNV_VK_INVALID;goto done;}}
    }
    VkPipelineDepthStencilStateCreateInfo depth_info;hr=fnv_d3d9_vk_depth(&d->depth_state,&depth_info);if(hr)goto done;
    VkPipelineColorBlendAttachmentState blend_attachment;float blend_constants[4];hr=fnv_d3d9_vk_blend(&d->blend_state,&blend_attachment,blend_constants);if(hr)goto done;
    if(blend_attachment.blendEnable){VkFormatProperties props;d->v.GetPhysicalDeviceFormatProperties(d->physical,VK_FORMAT_B8G8R8A8_UNORM,&props);
        if(!(props.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)){hr=FNV_VK_INVALID;goto done;}}
    if(d->count && (hr=draw_slot(d,&watch))!=0)goto done;
    if((hr=flush_locked(d))!=0 || (hr=draw_slot(d,&watch))!=0)goto done;
    for(unsigned i=0;i<d->config.frames;++i)if(!d->frames[i].pending){f=d->frames+i;break;}
    if(!f){hr=FNV_VK_BUSY;goto done;}
    f->draw=calloc(1,sizeof(*f->draw));if(!f->draw){hr=FNV_VK_OOM;goto done;}struct draw_resources *r=f->draw;
    draw_pin(d,r,color->id);if(depth)draw_pin(d,r,depth->id);draw_pin(d,r,vs->id);draw_pin(d,r,ps->id);draw_pin(d,r,decl->id);
    for(unsigned i=0;i<16;++i)if(stream_mask&(1u<<i)){struct buffer_resource *b=up_vertex?(i?NULL:up_vertex):find_buffer(d,streams[i].id);r->vertices[i]=b->version;r->vertices[i]->refs++;draw_pin(d,r,b->id);}
    if(indexed){r->indices=ib->version;r->indices->refs++;draw_pin(d,r,ib->id);}
    for(unsigned i=0;i<16;++i)if(program->mask&(1u<<i)){struct texture_resource *t=sampled[i];
        r->textures[i]=t->version;r->textures[i]->refs++;draw_pin(d,r,t->id);}
    const struct fnv_uniform_plan *plans[2]={&program->stages[0]->plan,&program->stages[1]->plan};
    struct fnv_uniform_draw snapshot;hr=(automatic || screen)?fnv_uniform_state_prepare_fixed(d->uniforms,fixed,fixed_bytes,&snapshot):fixed?fnv_uniform_state_prepare_vertex(d->uniforms,fixed,fixed_bytes,&snapshot):fnv_uniform_state_prepare_program(d->uniforms,plans,&snapshot);if(hr)goto rollback;
    for(unsigned i=0;i<2 && !hr;++i)if(snapshot.bytes[i])hr=make_version(d,i?FNV_D3D9_PS:FNV_D3D9_VS,snapshot.bytes[i],snapshot.data[i],r->ubo+i);
    {uint32_t cancel=fnv_uniform_state_cancel(d->uniforms,snapshot.ticket);if(cancel && !hr)hr=cancel;}if(hr)goto rollback;
    hr=draw_descriptors(d,r,program);if(hr)goto rollback;
    VkDescriptorSet descriptors[3];VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=r->descriptors,.descriptorSetCount=3,.pSetLayouts=r->sets+1};
    hr=result(d,d->v.AllocateDescriptorSets(d->device,&ai,descriptors));if(hr)goto rollback;
    VkDescriptorBufferInfo buffers[2];VkDescriptorImageInfo images[16];VkWriteDescriptorSet writes[18];uint32_t write_count=0;
    for(unsigned i=0;i<2;++i)if(r->ubo[i]){buffers[i]=(VkDescriptorBufferInfo){r->ubo[i]->buffer,0,snapshot.bytes[i]};
        writes[write_count++]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors[i?2:0],.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=buffers+i};}
    for(unsigned i=0;i<16;++i)if(program->mask&(1u<<i)){VkSampler sampler=VK_NULL_HANDLE;hr=result(d,d->v.CreateSampler(d->device,sampler_info+i,NULL,&sampler));if(hr)goto rollback;r->samplers[i]=sampler;
        images[i]=(VkDescriptorImageInfo){sampler,r->textures[i]->view,VK_IMAGE_LAYOUT_GENERAL};writes[write_count++]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=descriptors[1],.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=images+i};}
    d->v.UpdateDescriptorSets(d->device,write_count,writes,0,NULL);
    f->count=1;memset(f->commands,0,sizeof(*f->commands));hr=make_pass(d,f,0,color,depth);if(hr)goto rollback;
    float min_depth,max_depth;memcpy(&min_depth,d->viewport+4,4);memcpy(&max_depth,d->viewport+5,4);
    /* D3D9 samples integer pixel centers; Vulkan samples half-integers.
     * Move the post-projection viewport by half a pixel in framebuffer space.
     * Keep guest viewport/scissor values and shader constants unchanged. */
    VkViewport viewport={(float)d->viewport[0]+.5f,(float)(d->viewport[1]+d->viewport[3])+.5f,(float)d->viewport[2],-(float)d->viewport[3],min_depth,max_depth};
    VkRect2D scissor={{0,0},{color->width,color->height}};if(d->scissor_enabled)scissor=(VkRect2D){{(int32_t)d->scissor[0],(int32_t)d->scissor[1]},{d->scissor[2]-d->scissor[0],d->scissor[3]-d->scissor[1]}};
    VkShaderModule modules[2];hr=program_module(d,program->stages[0],vs,modules);if(!hr)hr=program_module(d,program->stages[1],ps,modules+1);if(hr)goto rollback;
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main"}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=binding_count,.pVertexBindingDescriptions=bindings,.vertexAttributeDescriptionCount=program->count,.pVertexAttributeDescriptions=attributes};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=primitive==4?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:primitive==5?VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN};
    VkPipelineViewportStateCreateInfo vp={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=d->cull_mode==1?VK_CULL_MODE_NONE:VK_CULL_MODE_BACK_BIT,.frontFace=d->cull_mode==2?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend_attachment};memcpy(bs.blendConstants,blend_constants,sizeof(blend_constants));
    VkGraphicsPipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,.pInputAssemblyState=&ia,
        .pViewportState=&vp,.pRasterizationState=&rs,.pMultisampleState=&ms,.pDepthStencilState=&depth_info,.pColorBlendState=&bs,.layout=r->layout,.renderPass=f->passes[0]};
    VkPipeline pipeline=VK_NULL_HANDLE;VkResult built=d->v.CreateGraphicsPipelines(d->device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline);
    /* Pipelines can be returned on failure, unlike ordinary create calls. */
    if(pipeline)r->pipeline=pipeline;
    hr=result(d,built);if(hr)goto rollback;
    {struct frame *actual=NULL;hr=begin_frame(d,&actual);if(hr)goto rollback;if(actual!=f){hr=FNV_VK_INVALID;goto rollback;}}
    barrier(d,f->cmd,color);if(depth)barrier(d,f->cmd,depth);
    VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_UNIFORM_READ_BIT};
    d->v.CmdPipelineBarrier(f->cmd,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&host,0,NULL,0,NULL);
    for(unsigned i=0;i<16;++i)if(r->textures[i])texture_barrier(d,f->cmd,r->textures[i],VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,sampled[i]->levels);
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=f->passes[0],.framebuffer=f->buffers[0],.renderArea={{0,0},{color->width,color->height}}};
    d->v.CmdBeginRenderPass(f->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);d->v.CmdBindPipeline(f->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,r->pipeline);
    d->v.CmdBindDescriptorSets(f->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,r->layout,1,3,descriptors,0,NULL);
    for(unsigned i=0;i<16;++i)if(r->vertices[i]){VkDeviceSize offset=streams[i].offset;d->v.CmdBindVertexBuffers(f->cmd,i,1,&r->vertices[i]->buffer,&offset);}
    if(indexed){d->v.CmdBindIndexBuffer(f->cmd,r->indices->buffer,0,ib->format==101?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);
        int64_t signed_base=base&0x80000000u?(int64_t)base-0x100000000ll:base;d->v.CmdDrawIndexed(f->cmd,access.count,1,start,(int32_t)signed_base,0);}
    else d->v.CmdDraw(f->cmd,access.count,1,start,0);
    d->v.CmdEndRenderPass(f->cmd);hr=submit_frame(d,f);if(hr){d->lost=1;goto done;}if(color->texture)color->texture->initialized=1;d->draws++;goto done;
rollback:
    if(!d->lost && f)retire_frame(d,f);
done:return hr;
}

static uint32_t draw(void *ctx,int indexed,uint32_t primitive,uint32_t base,uint32_t min_vertex,uint32_t vertices,uint32_t start,uint32_t primitives){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);
    uint32_t hr=draw_locked(d,indexed,primitive,base,min_vertex,vertices,start,primitives,NULL,NULL,0,NULL,0);
    fnv_host_mutex_unlock(&d->guard);return hr;
}
#include "d3d9_vk_up.inc"

static uint32_t draw_fixed_unlit(void *ctx,int indexed,uint32_t primitive,uint32_t base,uint32_t minimum,uint32_t vertices,uint32_t start,uint32_t primitives,const float columns[16]){
    struct fnv_vk_backend *d=ctx;if(!d || !columns)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);
    uint32_t hr=draw_locked(d,indexed,primitive,base,minimum,vertices,start,primitives,NULL,NULL,0,columns,64);
    fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t fixed_vertex_register(struct fnv_vk_backend *d,uintptr_t id,int directional){
    if(!d)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    struct shader_resource *s=find_shader(d,id);
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(id){
        if(!s || s->entry->stage!=FNV_D3D9_VERTEX || s->entry->plan.bytes!=(directional?160u:64u))goto done;
        uint32_t mask=0,registers=directional?10:4;
        for(unsigned i=0;i<s->entry->plan.count;i++){
            const struct fnv_uniform_entry *e=s->entry->plan.entries+i;
            if(e->kind || !e->count || e->first>=registers || e->count>registers-e->first || e->offset!=e->first*16 || e->stride!=16)goto done;
            for(unsigned j=0;j<e->count;j++){uint32_t bit=1u<<(e->first+j);if(mask&bit)goto done;mask|=bit;}
        }
        if(mask!=((1u<<registers)-1))goto done;
    }
    uintptr_t *slot=directional?&d->fixed_directional:&d->fixed_vertex;
    pin(d,id,1);uintptr_t old=*slot;*slot=id;pin(d,old,0);hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}

uint32_t fnv_vk_backend_fixed_vertex(struct fnv_vk_backend *d,uintptr_t id){return fixed_vertex_register(d,id,0);}
uint32_t fnv_vk_backend_fixed_directional(struct fnv_vk_backend *d,uintptr_t id){return fixed_vertex_register(d,id,1);}
uint32_t fnv_vk_backend_fixed_screen(struct fnv_vk_backend *d,uintptr_t vertex,uintptr_t pixel){
    if(!d || (!vertex)!=(!pixel))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    struct shader_resource *vs=find_shader(d,vertex),*ps=find_shader(d,pixel);
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(vertex){
        if(!vs || !ps || vs->entry->stage!=FNV_D3D9_VERTEX || ps->entry->stage!=FNV_D3D9_PIXEL ||
           vs->entry->plan.bytes!=16 || vs->entry->plan.count!=1 || ps->entry->plan.bytes || ps->entry->plan.count)goto done;
        const struct fnv_uniform_entry *e=vs->entry->plan.entries;
        if(e->kind || e->first || e->count!=1 || e->offset || e->stride!=16)goto done;
    }
    pin(d,vertex,1);pin(d,pixel,1);uintptr_t old[2]={d->fixed_screen[0],d->fixed_screen[1]};
    d->fixed_screen[0]=vertex;d->fixed_screen[1]=pixel;pin(d,old[0],0);pin(d,old[1],0);hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t draw_fixed_directional(void *ctx,int indexed,uint32_t primitive,uint32_t base,uint32_t minimum,uint32_t vertices,uint32_t start,uint32_t primitives,const float constants[40]){
    struct fnv_vk_backend *d=ctx;if(!d || !constants)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=(d->lost || !d->device_refs)?FNV_VK_LOST:d->fixed_directional?draw_locked(d,indexed,primitive,base,minimum,vertices,start,primitives,NULL,NULL,0,constants,160):0x8876086au;
    fnv_host_mutex_unlock(&d->guard);return hr;
}

uint32_t fnv_vk_backend_fixed_pixel(struct fnv_vk_backend *d,uint32_t mode,uintptr_t id){
    if(!d || mode>=4)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    struct shader_resource *s=find_shader(d,id);
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(id && (!s || s->entry->stage!=FNV_D3D9_PIXEL || s->entry->plan.bytes || s->entry->plan.count))goto done;
    pin(d,id,1);uintptr_t old=d->fixed_pixel[mode];d->fixed_pixel[mode]=id;pin(d,old,0);hr=0;
 done:fnv_host_mutex_unlock(&d->guard);return hr;
}
