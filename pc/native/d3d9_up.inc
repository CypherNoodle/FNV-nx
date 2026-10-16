/* CPU guest ranges are checked before the provider copies transient data. */
static void draw_up(int indexed){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),primitive=fnv_cpu_argument_u32(1);
    uint32_t minimum=indexed?fnv_cpu_argument_u32(2):0,vertices=indexed?fnv_cpu_argument_u32(3):0;
    uint32_t primitives=fnv_cpu_argument_u32(indexed?4:2),index_at=indexed?fnv_cpu_argument_u32(5):0,format=indexed?fnv_cpu_argument_u32(6):0;
    uint32_t vertex_at=fnv_cpu_argument_u32(indexed?7:3),stride=fnv_cpu_argument_u32(indexed?8:4),hr=FNV_D3D9_INVALIDCALL;
    uint64_t count=primitive==4?(uint64_t)primitives*3:(uint64_t)primitives+2;
    uint64_t vertex_bytes=(indexed?(uint64_t)minimum+vertices:count)*stride,index_bytes=indexed?count*(format==101?2u:4u):0;
    unsigned char *data=NULL,*indices=NULL;
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(!c->config.backend.draw_up || !c->config.backend.upload_constants){hr=0x8876086au;goto done;}
    if(!c->in_scene || primitive<4 || primitive>6 || !primitives || !stride || count>UINT32_MAX || vertex_bytes>UINT32_MAX || index_bytes>UINT32_MAX ||
       (indexed && (!vertices || (uint64_t)minimum+vertices>UINT32_MAX || (format!=101 && format!=102))) || c->declaration<0)goto done;
    struct resource *declaration=c->resources+c->declaration;
    for(uint32_t i=0;i+1<declaration->element_count;++i)if(read32(declaration->elements+i*8)&0xffffu)goto done;
    for(unsigned i=0;i<16;++i)if(c->textures[i]>=0){struct resource *r=c->resources+c->textures[i];for(uint32_t level=0;level<r->levels;++level)if(r->mips[level].locked)goto done;}
    data=range(c,vertex_at,(uint32_t)vertex_bytes,0);if(!data)goto done;
    if(indexed){indices=range(c,index_at,(uint32_t)index_bytes,0);if(!indices)goto done;}
    hr=fnv_d3d9_constants_flush(c->constants,c->config.backend.upload_constants,c->config.backend.context);if(hr)goto done;
    hr=c->config.backend.draw_up(c->config.backend.context,indexed,primitive,minimum,vertices,primitives,indices,format,data,stride,(uint32_t)vertex_bytes,(uint32_t)index_bytes);
    /* Provider clears the same bindings after a dispatched UP attempt. */
    change_binding(c,&c->streams[0].index,-1);c->streams[0].offset=c->streams[0].stride=0;
    if(indexed)change_binding(c,&c->indices,-1);
done:fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,indexed?36:20);
}
static void api_draw_up(void){draw_up(0);}
static void api_draw_indexed_up(void){draw_up(1);}
