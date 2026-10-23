/* Private shader/declaration provider. Catalog binaries are offline trusted inputs. */
static uint32_t word(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static int shader_header(enum fnv_d3d9_stage stage,const void *bytes,uint32_t count){
    if((unsigned)stage>1 || !bytes || count<8 || count%4 || count>1048576)return 0;
    uint32_t token=word(bytes);return (token>>16)==(stage?0xffffu:0xfffeu) &&
        ((token>>8)&255)>=1 && ((token>>8)&255)<=3 && word((const unsigned char*)bytes+count-4)==0xffffu;
}
static int spirv_header(enum fnv_d3d9_stage stage,const void *bytes,uint32_t count){
    if(!bytes || count<20 || count%4 || count>1048576)return 0;
    const unsigned char *p=bytes;
    if(word(p)!=0x07230203 || word(p+4)<0x10000 || word(p+4)>0x10300 || !word(p+12) || word(p+16))return 0;
    unsigned entries=0;
    for(uint32_t at=20;at<count;){uint32_t instruction=word(p+at),length=instruction>>16,opcode=instruction&65535;
        if(!length || length>(count-at)/4)return 0;
        if(opcode==15){if(length<5 || word(p+at+4)!=(stage?4u:0u) || word(p+at+8)>=word(p+12) || memcmp(p+at+12,"main",5))return 0;entries++;}
        at+=length*4;
    }
    return entries==1;
}
static int same_shader_code(const struct shader_catalog *a,const struct shader_catalog *b){return a==b || (a->stage==b->stage && a->code_bytes==b->code_bytes && !memcmp(a->code,b->code,a->code_bytes));}
static int shader_payload(const struct shader_catalog *e,const void *spirv,uint32_t bytes,const struct fnv_uniform_plan *plan){
    if(!plan || (plan->count && !plan->entries) || e->spirv_bytes!=bytes || memcmp(e->spirv,spirv,bytes) || e->plan.bytes!=plan->bytes || e->plan.count!=plan->count)return 0;
    for(uint32_t i=0;i<plan->count;i++){
        const struct fnv_uniform_entry *a=e->entries+i,*b=plan->entries+i;
        if(a->kind!=b->kind || a->first!=b->first || a->count!=b->count || a->offset!=b->offset || a->stride!=b->stride)return 0;
    }
    return 1;
}
/* Trusted host admission of offline-validated D3D9 tokens. No GPU executable is
 * implied: Draw still requires an explicitly linked, prepared program. */
uint32_t fnv_vk_backend_declare_shader(struct fnv_vk_backend *d,enum fnv_d3d9_stage stage,const void *code,uint32_t bytes){
    if(!d || !shader_header(stage,code,bytes))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    for(struct shader_catalog *e=d->catalog;e;e=e->next)if(e->stage==stage && e->code_bytes==bytes && !memcmp(e->code,code,bytes)){hr=0;goto done;}
    if(d->catalog_count>=4096){hr=FNV_VK_OOM;goto done;}
    struct shader_catalog *entry=calloc(1,sizeof(*entry));if(!entry){hr=FNV_VK_OOM;goto done;}
    entry->code=malloc(bytes);if(!entry->code){free(entry);hr=FNV_VK_OOM;goto done;}
    memcpy(entry->code,code,bytes);entry->code_bytes=bytes;entry->stage=stage;
    entry->next=d->catalog;d->catalog=entry;d->catalog_count++;hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t register_shader(struct fnv_vk_backend *d,enum fnv_d3d9_stage stage,const void *code,uint32_t code_bytes,const void *spirv,uint32_t spirv_bytes,const struct fnv_uniform_plan *plan,int reuse){
    if(!d || !shader_header(stage,code,code_bytes) || !spirv_header(stage,spirv,spirv_bytes) || !plan || (plan->count && !plan->entries))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(!reuse && d->catalog_count>=4096){hr=FNV_VK_OOM;goto done;}
    int duplicate=0;
    for(struct shader_catalog *e=d->catalog;e;e=e->next)if(e->stage==stage && e->code_bytes==code_bytes && !memcmp(e->code,code,code_bytes)){
        if(!e->spirv_bytes)continue;
        duplicate=1;if(!reuse)goto done;
        if(shader_payload(e,spirv,spirv_bytes,plan)){hr=0;goto done;}
    }
    if(duplicate && reuse!=2)goto done;
    if(d->catalog_count>=4096){hr=FNV_VK_OOM;goto done;}
    if(plan->bytes>d->properties.limits.maxUniformBufferRange || plan->bytes>65536 || plan->count>512)goto done;
    uint32_t zeros[1024]={0};struct fnv_uniform_banks banks={{zeros,zeros,zeros},{stage?224:256,16,16}};
    void *scratch=plan->bytes?malloc(plan->bytes):NULL;if(plan->bytes && !scratch){hr=FNV_VK_OOM;goto done;}
    hr=fnv_d3d9_uniform_pack(plan,&banks,scratch,plan->bytes);free(scratch);if(hr)goto done;
    struct shader_catalog *entry=calloc(1,sizeof(*entry));if(!entry){hr=FNV_VK_OOM;goto done;}
    entry->code=malloc(code_bytes);entry->spirv=malloc(spirv_bytes);entry->entries=plan->count?malloc((size_t)plan->count*sizeof(*entry->entries)):NULL;
    if(!entry->code || !entry->spirv || (plan->count && !entry->entries)){free(entry->code);free(entry->spirv);free(entry->entries);free(entry);hr=FNV_VK_OOM;goto done;}
    memcpy(entry->code,code,code_bytes);memcpy(entry->spirv,spirv,spirv_bytes);
    if(plan->count)memcpy(entry->entries,plan->entries,(size_t)plan->count*sizeof(*entry->entries));
    entry->plan=*plan;entry->plan.entries=entry->entries;entry->code_bytes=code_bytes;entry->spirv_bytes=spirv_bytes;entry->stage=stage;
    entry->next=d->catalog;d->catalog=entry;d->catalog_count++;hr=0;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_register_shader(struct fnv_vk_backend *d,enum fnv_d3d9_stage stage,const void *code,uint32_t code_bytes,const void *spirv,uint32_t spirv_bytes,const struct fnv_uniform_plan *plan){return register_shader(d,stage,code,code_bytes,spirv,spirv_bytes,plan,0);}
uint32_t fnv_vk_backend_ensure_shader(struct fnv_vk_backend *d,enum fnv_d3d9_stage stage,const void *code,uint32_t code_bytes,const void *spirv,uint32_t spirv_bytes,const struct fnv_uniform_plan *plan){return register_shader(d,stage,code,code_bytes,spirv,spirv_bytes,plan,1);}
static uint32_t create_shader(void *ctx,enum fnv_d3d9_stage stage,const void *code,uint32_t bytes,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;if(!out || !shader_header(stage,code,bytes))return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(d->next_id==UINTPTR_MAX)goto done;
    struct shader_catalog *entry=d->catalog;
    while(entry && (entry->stage!=stage || entry->code_bytes!=bytes || memcmp(entry->code,code,bytes)))entry=entry->next;
    if(!entry)goto done;
    if(resource_count(d)>=d->config.resource_capacity){hr=FNV_VK_OOM;goto done;}
    struct shader_resource *s=NULL;for(uint32_t i=0;i<d->config.resource_capacity;++i)if(!d->shaders[i].id){s=d->shaders+i;break;}
    VkShaderModuleCreateInfo ci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=entry->spirv_bytes,.pCode=entry->spirv};
    VkShaderModule module=VK_NULL_HANDLE;hr=entry->spirv_bytes?result(d,d->v.CreateShaderModule(d->device,&ci,NULL,&module)):0;if(hr)goto done;
    *s=(struct shader_resource){.id=++d->next_id,.refs=1,.entry=entry,.module=module};d->device_refs++;*out=s->id;
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t bind_shader(void *ctx,enum fnv_d3d9_stage stage,uintptr_t id){
    struct fnv_vk_backend *d=ctx;if((unsigned)stage>1)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;struct shader_resource *s=find_shader(d,id);
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(id && (!s || s->entry->stage!=stage))goto done;
    hr=fnv_uniform_state_bind(d->uniforms,stage,s?&s->entry->plan:NULL);if(hr)goto done;
    uintptr_t old=d->bound_shaders[stage];pin(d,id,1);d->bound_shaders[stage]=id;pin(d,old,0);
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static int upload_constants(void *ctx,enum fnv_d3d9_stage stage,enum fnv_d3d9_constant_kind kind,uint32_t first,uint32_t count,const uint32_t *bits){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);
    int accepted=!d->lost && d->device_refs && fnv_uniform_state_upload(d->uniforms,stage,kind,first,count,bits);
    fnv_host_mutex_unlock(&d->guard);return accepted;
}
static uint32_t create_declaration(void *ctx,const void *elements,uint32_t count,uintptr_t *out){
    struct fnv_vk_backend *d=ctx;if(!elements || !out || count<1 || count>65 || count-1>d->properties.limits.maxVertexInputAttributes)return FNV_VK_INVALID;
    struct declaration_resource next={.refs=1,.count=count};memcpy(next.elements,elements,count*8);
    const VkFormat formats[17]={VK_FORMAT_R32_SFLOAT,VK_FORMAT_R32G32_SFLOAT,VK_FORMAT_R32G32B32_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_R8G8B8A8_USCALED,VK_FORMAT_R16G16_SSCALED,VK_FORMAT_R16G16B16A16_SSCALED,
        VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16_SNORM,VK_FORMAT_R16G16B16A16_SNORM,VK_FORMAT_R16G16_UNORM,VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_UNDEFINED,VK_FORMAT_UNDEFINED,VK_FORMAT_R16G16_SFLOAT,VK_FORMAT_R16G16B16A16_SFLOAT};
    for(uint32_t i=0;i<count;++i){const unsigned char *e=next.elements+i*8;uint32_t stream=e[0]|(uint32_t)e[1]<<8,offset=e[2]|(uint32_t)e[3]<<8;
        if(i==count-1){const unsigned char end[8]={255,0,0,0,17,0,0,0};if(memcmp(e,end,8))return FNV_VK_INVALID;break;}
        if(stream>=16 || stream>=d->properties.limits.maxVertexInputBindings || e[4]>=17 || !formats[e[4]] || e[5] || e[6]>13 || e[7]>15 || offset>d->properties.limits.maxVertexInputAttributeOffset)return FNV_VK_INVALID;
        for(uint32_t j=0;j<i;++j)if(next.elements[j*8+6]==e[6] && next.elements[j*8+7]==e[7])return FNV_VK_INVALID;
        next.formats[i]=formats[e[4]];
    }
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs){hr=FNV_VK_LOST;goto done;}
    if(d->next_id==UINTPTR_MAX)goto done;
    for(uint32_t i=0;i<count-1;++i){VkFormatProperties props;d->v.GetPhysicalDeviceFormatProperties(d->physical,next.formats[i],&props);
        if(!(props.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))goto done;}
    if(resource_count(d)>=d->config.resource_capacity){hr=FNV_VK_OOM;goto done;}
    for(uint32_t i=0;i<d->config.resource_capacity;++i)if(!d->declarations[i].id){next.id=++d->next_id;d->declarations[i]=next;d->device_refs++;*out=next.id;hr=0;break;}
done:fnv_host_mutex_unlock(&d->guard);return hr;
}
static uint32_t bind_declaration(void *ctx,uintptr_t id){
    struct fnv_vk_backend *d=ctx;fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_INVALID;
    if(d->lost || !d->device_refs)hr=FNV_VK_LOST;
    else if(!id || find_declaration(d,id)){uintptr_t old=d->declaration;pin(d,id,1);d->declaration=id;pin(d,old,0);hr=0;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_read_declaration(struct fnv_vk_backend *d,uintptr_t id,void *out,uint32_t capacity){
    if(!d || !out)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);struct declaration_resource *v=find_declaration(d,id);uint32_t hr=FNV_VK_INVALID;
    if(d->lost)hr=FNV_VK_LOST;else if(v && capacity>=v->count*8){memcpy(out,v->elements,v->count*8);hr=0;}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
uint32_t fnv_vk_backend_read_uniforms(struct fnv_vk_backend *d,void *vertex,uint32_t vertex_capacity,void *pixel,uint32_t pixel_capacity){
    if(!d)return FNV_VK_INVALID;
    fnv_host_mutex_lock(&d->guard);uint32_t hr=FNV_VK_LOST;
    if(!d->lost && d->device_refs){struct fnv_uniform_draw snapshot;hr=fnv_uniform_state_prepare(d->uniforms,&snapshot);
        if(!hr){uintptr_t vs=(uintptr_t)vertex,ps=(uintptr_t)pixel;uint32_t vb=snapshot.bytes[0],pb=snapshot.bytes[1];
            if(vb>vertex_capacity || pb>pixel_capacity || (vb && !vertex) || (pb && !pixel) || vb>UINTPTR_MAX-vs || pb>UINTPTR_MAX-ps || (vb && pb && vs<ps+pb && ps<vs+vb))hr=FNV_VK_INVALID;
            uint32_t cancel=fnv_uniform_state_cancel(d->uniforms,snapshot.ticket);if(cancel)hr=cancel;
            /* Cancel only releases the ticket; storage is unchanged. The outer
             * backend lock excludes every other access to this private bank. */
            if(!hr){if(vb)memcpy(vertex,snapshot.data[0],vb);if(pb)memcpy(pixel,snapshot.data[1],pb);}
        }}
    fnv_host_mutex_unlock(&d->guard);return hr;
}
