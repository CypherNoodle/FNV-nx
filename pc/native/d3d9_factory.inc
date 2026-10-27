/* Always-on diagnostic trace: the sink is a host hook installed by the Switch
 * probe. Horizon does not capture stderr and the factory path is the object of
 * study, so this is not a compile-time option and never fabricates success. */
#define FACTORY_TRACE(...) fnv_d3d9_trace(__VA_ARGS__)
/* Single prepared virtual-adapter factory. All methods registered before attach. */
#define NOT_AVAILABLE 0x8876086au
static uint32_t factory_address(const struct fnv_d3d9_com *c){return c->config.objects_address+FACTORY_OBJECT;}
static void factory_this(struct fnv_d3d9_com *c,uint32_t self){if(object(c,self,0)!=-2){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 factory requires IDirect3D9",self);}}
static void api_direct3d9(void){
    uint32_t version=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();uint32_t result=0,refs;
    fnv_host_mutex_lock(&c->guard);if(c->config.factory && c->factory_refs && c->factory_refs<UINT32_MAX && version==32){c->factory_refs++;result=factory_address(c);}
    refs=c->factory_refs;fnv_host_mutex_unlock(&c->guard);
    FACTORY_TRACE("Direct3DCreate9 version=%u result=%08x refs=%u\n",version,result,refs);fnv_cpu_return_u32(result,4);
}
static void api_adapter_count(void){struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(1,4);}
/* Enumeration ignores display alpha (EnumAdapterModes contract). The
 * prepared render target remains A8R8G8B8; this does not add an XRGB target. */
static int factory_display_format(const struct fnv_d3d9_com *c,uint32_t format){
    return format==c->factory.color_format || (c->factory.color_format==21 && format==22);
}
static void api_mode_count(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),format=fnv_cpu_argument_u32(2);struct fnv_d3d9_com *c=module();
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);uint32_t result=!adapter && factory_display_format(c,format);fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("GetAdapterModeCount adapter=%u format=%u count=%u\n",adapter,format,result);fnv_cpu_return_u32(result,12);
}
static void display_mode(int enumerate){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),format=enumerate?fnv_cpu_argument_u32(2):0,index=enumerate?fnv_cpu_argument_u32(3):0;
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,fnv_cpu_argument_u32(enumerate?4:2),16,1);uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);
    if(out && !adapter && !index && (!enumerate || factory_display_format(c,format))){const uint32_t values[4]={c->factory.width,c->factory.height,c->factory.refresh_hz,enumerate?format:c->factory.color_format};for(unsigned i=0;i<4;++i)write32(out+i*4,values[i]);result=0;}
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("DisplayMode enumerate=%d adapter=%u format=%u index=%u hr=%08x\n",enumerate,adapter,format,index,result);fnv_cpu_return_u32(result,enumerate?20:12);
}
static void api_enum_modes(void){display_mode(1);}
static void api_display_mode(void){display_mode(0);}
static void api_check_type(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),type=fnv_cpu_argument_u32(2),display=fnv_cpu_argument_u32(3),back=fnv_cpu_argument_u32(4),windowed=fnv_cpu_argument_u32(5);
    struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);factory_this(c,self);
    uint32_t result=adapter?FNV_D3D9_INVALIDCALL:type==1 && factory_display_format(c,display) && (back==c->factory.color_format || (windowed && !back))?0:NOT_AVAILABLE;
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("CheckDeviceType adapter=%u type=%u display=%u back=%u windowed=%u hr=%08x\n",adapter,type,display,back,windowed,result);fnv_cpu_return_u32(result,24);
}
static void api_check_format(void){
    struct fnv_d3d9_com *c=module();uint32_t args[6];for(unsigned i=0;i<6;++i)args[i]=fnv_cpu_argument_u32(i+1);
    uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);uint32_t result=NOT_AVAILABLE;
    if(args[0])result=FNV_D3D9_INVALIDCALL;
    else if(args[1]==1 && factory_display_format(c,args[2]) && c->factory.query)result=c->factory.query(c->factory.context,0,args,6,NULL,0);
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("CheckDeviceFormat adapter=%u type=%u adapter_format=%u usage=%u resource=%u check=%u hr=%08x\n",args[0],args[1],args[2],args[3],args[4],args[5],result);fnv_cpu_return_u32(result,28);
}
static void get_caps(int factory){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),args[2]={factory?fnv_cpu_argument_u32(1):0,factory?fnv_cpu_argument_u32(2):1};
    unsigned char *out=range(c,fnv_cpu_argument_u32(factory?3:1),304,1);unsigned char candidate[304]={0};uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);if(factory)factory_this(c,self);else (void)object(c,self,1);
    if(out && !args[0]){result=NOT_AVAILABLE;if(args[1]==1 && c->factory.query)result=c->factory.query(c->factory.context,1,args,2,candidate,sizeof(candidate));
        if(!result)memcpy(out,candidate,sizeof(candidate));}
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("GetDeviceCaps factory=%d adapter=%u type=%u hr=%08x\n",factory,args[0],args[1],result);fnv_cpu_return_u32(result,factory?16:8);
}
static void api_factory_caps(void){get_caps(1);}
static void api_device_caps(void){get_caps(0);}
static void unavailable(unsigned count,int monitor){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("Unsupported argument_count=%u monitor=%d hr=%08x\n",count,monitor,monitor?0:NOT_AVAILABLE);fnv_cpu_return_u32(monitor?0:NOT_AVAILABLE,count*4);
}
static void api_unavailable2(void){unavailable(2,0);}
static void api_adapter_identifier(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),args[2]={fnv_cpu_argument_u32(1),fnv_cpu_argument_u32(2)};
    unsigned char *out=range(c,fnv_cpu_argument_u32(3),1104,1),candidate[1104]={0};uint32_t hr=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);
    if(out && args[0])memset(out,0,1104);
    else if(out && !(args[1]&~2u)){
        hr=c->factory.query?c->factory.query(c->factory.context,2,args,2,candidate,sizeof(candidate)):NOT_AVAILABLE;
        if(!hr)memcpy(out,candidate,sizeof(candidate));
    }
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("GetAdapterIdentifier adapter=%u flags=%u hr=%08x\n",args[0],args[1],hr);fnv_cpu_return_u32(hr,16);
}
static void api_unavailable5(void){unavailable(5,0);}
static uint32_t factory_surface_format(struct fnv_d3d9_com *c,uint32_t format,uint32_t usage){
    uint32_t args[6]={0,1,c->factory.color_format,usage,1,format};
    return c->factory.query?c->factory.query(c->factory.context,0,args,6,NULL,0):NOT_AVAILABLE;
}
static void api_depth_match(void){
    struct fnv_d3d9_com *c=module();uint32_t args[6];for(unsigned i=0;i<6;++i)args[i]=fnv_cpu_argument_u32(i);
    fnv_host_mutex_lock(&c->guard);factory_this(c,args[0]);uint32_t hr=NOT_AVAILABLE;
    if(args[1])hr=FNV_D3D9_INVALIDCALL;
    else if(args[2]==1 && factory_display_format(c,args[3]) && args[4]==21 && args[5]==75){
        hr=factory_surface_format(c,args[4],1);if(!hr)hr=factory_surface_format(c,args[5],2);
    }
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("CheckDepthStencilMatch adapter=%u type=%u adapter_format=%u render_format=%u depthstencil=%u hr=%08x\n",args[1],args[2],args[3],args[4],args[5],hr);fnv_cpu_return_u32(hr,24);
}
static void api_multisample(void){
    struct fnv_d3d9_com *c=module();uint32_t args[7];for(unsigned i=0;i<7;++i)args[i]=fnv_cpu_argument_u32(i);
    unsigned char *out=args[6]?range(c,args[6],4,1):NULL;
    fnv_host_mutex_lock(&c->guard);factory_this(c,args[0]);uint32_t hr=NOT_AVAILABLE;
    if(args[1] || args[5]>16 || (args[6] && !out))hr=FNV_D3D9_INVALIDCALL;
    /* D3DMULTISAMPLE_NONE is valid for every supported render format and has
     * exactly one quality level. No multisampled target is implemented, so any
     * nonzero sample count stays NOTAVAILABLE rather than inventing levels. */
    else if(args[2]==1 && !args[5] && (args[3]==21 || args[3]==22 || args[3]==75)){
        /* The configured presentation format is the real implicit target, so
         * it needs no synthetic Vulkan render-target claim. Other formats go
         * through the provider's actual usage/type/format query. */
        hr=args[3]==c->factory.color_format?0:factory_surface_format(c,args[3],args[3]==75?2:1);
        if(!hr && out)write32(out,1);
    }
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("CheckDeviceMultiSampleType adapter=%u type=%u format=%u windowed=%u multisample=%u out=%08x levels=%u hr=%08x\n",args[1],args[2],args[3],args[4],args[5],(unsigned)args[6],out?read32(out):0,hr);fnv_cpu_return_u32(hr,28);
}
static void api_adapter_monitor(void){unavailable(2,1);}
static void api_create_device(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),type=fnv_cpu_argument_u32(2),focus=fnv_cpu_argument_u32(3),behavior=fnv_cpu_argument_u32(4),at=fnv_cpu_argument_u32(5),out_at=fnv_cpu_argument_u32(6);
    struct fnv_d3d9_com *c=module();unsigned char *params=range(c,at,56,1),*out=range(c,out_at,4,1);uint32_t values[14],result=FNV_D3D9_INVALIDCALL;
    const char *why="ok";
    if(!params || !out || ((uint64_t)at<(uint64_t)out_at+4 && (uint64_t)out_at<(uint64_t)at+56)){FACTORY_TRACE("CreateDevice self=%08x invalid pointers at=%08x out=%08x\n",self,at,out_at);fnv_cpu_return_u32(result,28);return;}
    for(unsigned i=0;i<14;++i)values[i]=read32(params+i*4);
    FACTORY_TRACE("CreateDevice self=%08x adapter=%u type=%u focus=%08x behavior=%08x %ux%u color=%u buffers=%u msaa=%u quality=%u swap=%u window=%08x windowed=%u auto_depth=%u depth=%u flags=%08x refresh=%u interval=%u\n",self,adapter,type,focus,behavior,values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9],values[10],values[11],values[12],values[13]);
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);write32(out,0);c->create_attempts++;
    if(adapter){why="adapter is nonzero";goto done;}
    if(c->factory_created){why="factory already created a device";goto done;}
    if(!c->device_references){why="device was released";goto done;}
    if(type!=1){why="device type is not HAL";result=NOT_AVAILABLE;goto done;}
    values[8]=values[8]!=0;values[9]=values[9]!=0;
    /* Real D3D9 behaviour bits: FPU_PRESERVE(2), MULTITHREADED(4),
     * PUREDEVICE(16), SOFTWARE_VERTEXPROCESSING(32), hardware(64) and
     * mixed(128). PUREDEVICE is an application promise, so it is accepted only
     * with hardware vertex processing. Software and mixed processing are real
     * missing capabilities here (the provider only executes prepared GPU
     * programs), so they fail explicitly instead of being mapped to HAL. */
    uint32_t processing=behavior&(32u|64u|128u);
    if((behavior&~(2u|4u|16u|32u|64u|128u)) || (processing && processing!=32u && processing!=64u && processing!=128u) ||
       (!focus && !values[7]) || (!values[8] && !focus)){why="behavior/focus/window bits";goto done;}
    if((behavior&16u) && processing!=64u){why="pure device requires hardware vertex processing";goto done;}
    if(processing==32u){result=NOT_AVAILABLE;why="software vertex processing not implemented";goto done;}
    if(processing==128u){result=NOT_AVAILABLE;why="mixed vertex processing not implemented";goto done;}
    if(values[8]){if(!values[0])values[0]=c->factory.width;if(!values[1])values[1]=c->factory.height;if(!values[2])values[2]=c->factory.color_format;}
    if(!values[3])values[3]=1;
    if(values[0]!=c->factory.width || values[1]!=c->factory.height || values[2]!=c->factory.color_format || values[3]!=1 || values[4] || values[5] || values[6]!=1 ||
       (values[9]!=0)!=(c->factory.depth_format!=0) || (values[9] && values[10]!=c->factory.depth_format) || (values[11]&~3u) ||
       (values[8] && values[12]) || (!values[8] && values[12] && values[12]!=c->factory.refresh_hz) || values[13]>1){why="present parameters differ from prepared factory";result=NOT_AVAILABLE;goto done;}
    if(values[11]&1u){
        /* LOCKABLE is a real capability. It is accepted only with an installed
         * round-trip provider and a live implicit backbuffer identity; the
         * actual pixels are transferred on every Lock/Unlock. */
        if(!c->surface_io.read || !c->surface_io.write){result=NOT_AVAILABLE;why="lockable backbuffer transfer provider absent";goto done;}
        uintptr_t handle=0;result=c->config.backend.get_target(c->config.backend.context,0,0,&handle);
        if(result){why="lockable backbuffer target query";goto done;}
        c->lockable_handle=handle;c->lockable_back=1;c->config.backend.release(c->config.backend.context,handle);
        FACTORY_TRACE("CreateDevice lockable backbuffer handle=%08x\n",(unsigned)handle);
    }
    c->factory_created=1;c->creation[0]=adapter;c->creation[1]=type;c->creation[2]=focus;c->creation[3]=behavior;
    c->config.presentation_window=values[7]?values[7]:focus;
    c->creator=(uintptr_t)&creator_marker;memcpy(c->present_params,values,sizeof(values));
    for(unsigned i=0;i<14;++i)write32(params+i*4,values[i]);
    write32(out,device_address(c));result=0;
done:c->last_create_result=result;if(result)c->create_failures++;else c->create_successes++;
    uint32_t created=c->factory_created,refs=c->factory_refs,devrefs=c->device_references;
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("CreateDevice result=%08x behavior=%08x why=%s created=%u refs=%u devrefs=%u\n",result,behavior,why,created,refs,devrefs);fnv_cpu_return_u32(result,28);
}
static void api_device_parent(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);unsigned char *out=range(c,fnv_cpu_argument_u32(1),4,1);uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(out && c->config.factory && c->factory_refs && c->factory_refs<UINT32_MAX){c->factory_refs++;write32(out,factory_address(c));result=0;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
}
static void api_creation_parameters(void){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);unsigned char *out=range(c,fnv_cpu_argument_u32(1),16,1);uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(out && c->factory_created){for(unsigned i=0;i<4;++i)write32(out+i*4,c->creation[i]);result=0;}
    fnv_host_mutex_unlock(&c->guard);FACTORY_TRACE("GetCreationParameters self=%08x created=%u hr=%08x adapter=%u type=%u focus=%08x behavior=%08x\n",self,c->factory_created,result,c->creation[0],c->creation[1],c->creation[2],c->creation[3]);fnv_cpu_return_u32(result,8);
}
#undef NOT_AVAILABLE

static int factory_prepared(const struct fnv_d3d9_com_config *cfg,const struct fnv_d3d9_factory_profile *f){
    if(!cfg->backend.get_target || !cfg->backend.surface_desc)return 0;
    for(unsigned depth=0;depth<2;++depth){uintptr_t id=0;uint32_t desc[8];
        uint32_t hr=cfg->backend.get_target(cfg->backend.context,(int)depth,0,&id);
        if(depth && !f->depth_format){if(!hr && id)cfg->backend.release(cfg->backend.context,id);if(hr!=0x88760866u)return 0;continue;}
        if(hr || !id)return 0;
        hr=cfg->backend.surface_desc(cfg->backend.context,id,desc);cfg->backend.release(cfg->backend.context,id);
        if(hr || desc[0]!=(depth?f->depth_format:f->color_format) || desc[4] || desc[5] ||
           (depth?(desc[6]<f->width || desc[7]<f->height):(desc[6]!=f->width || desc[7]!=f->height)))return 0;
    }
    return 1;
}

#undef FACTORY_TRACE
