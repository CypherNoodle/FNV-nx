/* Single prepared virtual-adapter factory. All methods registered before attach. */
#define NOT_AVAILABLE 0x8876086au
static uint32_t factory_address(const struct fnv_d3d9_com *c){return c->config.objects_address+FACTORY_OBJECT;}
static void factory_this(struct fnv_d3d9_com *c,uint32_t self){if(object(c,self,0)!=-2){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 factory requires IDirect3D9",self);}}
static void api_direct3d9(void){
    uint32_t version=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();uint32_t result=0;
    fnv_host_mutex_lock(&c->guard);if(c->config.factory && c->factory_refs && c->factory_refs<UINT32_MAX && version==32){c->factory_refs++;result=factory_address(c);}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,4);
}
static void api_adapter_count(void){struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(1,4);}
static void api_mode_count(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),format=fnv_cpu_argument_u32(2);struct fnv_d3d9_com *c=module();
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);uint32_t result=!adapter && format==c->factory.color_format;fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void display_mode(int enumerate){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),format=enumerate?fnv_cpu_argument_u32(2):0,index=enumerate?fnv_cpu_argument_u32(3):0;
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,fnv_cpu_argument_u32(enumerate?4:2),16,1);uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);
    if(out && !adapter && !index && (!enumerate || format==c->factory.color_format)){const uint32_t values[4]={c->factory.width,c->factory.height,c->factory.refresh_hz,c->factory.color_format};for(unsigned i=0;i<4;++i)write32(out+i*4,values[i]);result=0;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,enumerate?20:12);
}
static void api_enum_modes(void){display_mode(1);}
static void api_display_mode(void){display_mode(0);}
static void api_check_type(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),type=fnv_cpu_argument_u32(2),display=fnv_cpu_argument_u32(3),back=fnv_cpu_argument_u32(4),windowed=fnv_cpu_argument_u32(5);
    struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);factory_this(c,self);
    uint32_t result=adapter?FNV_D3D9_INVALIDCALL:type==1 && display==c->factory.color_format && (back==c->factory.color_format || (windowed && !back))?0:NOT_AVAILABLE;
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,24);
}
static void api_check_format(void){
    struct fnv_d3d9_com *c=module();uint32_t args[6];for(unsigned i=0;i<6;++i)args[i]=fnv_cpu_argument_u32(i+1);
    uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);uint32_t result=NOT_AVAILABLE;
    if(args[0])result=FNV_D3D9_INVALIDCALL;
    else if(args[1]==1 && args[2]==c->factory.color_format && c->factory.query)result=c->factory.query(c->factory.context,0,args,6,NULL,0);
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,28);
}
static void get_caps(int factory){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),args[2]={factory?fnv_cpu_argument_u32(1):0,factory?fnv_cpu_argument_u32(2):1};
    unsigned char *out=range(c,fnv_cpu_argument_u32(factory?3:1),304,1);unsigned char candidate[304]={0};uint32_t result=FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);if(factory)factory_this(c,self);else (void)object(c,self,1);
    if(out && !args[0]){result=NOT_AVAILABLE;if(args[1]==1 && c->factory.query)result=c->factory.query(c->factory.context,1,args,2,candidate,sizeof(candidate));
        if(!result)memcpy(out,candidate,sizeof(candidate));}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,factory?16:8);
}
static void api_factory_caps(void){get_caps(1);}
static void api_device_caps(void){get_caps(0);}
static void unavailable(unsigned count,int monitor){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);fnv_host_mutex_lock(&c->guard);factory_this(c,self);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(monitor?0:NOT_AVAILABLE,count*4);
}
static void api_unavailable2(void){unavailable(2,0);}
static void api_unavailable4(void){unavailable(4,0);}
static void api_unavailable5(void){unavailable(5,0);}
static uint32_t factory_surface_format(struct fnv_d3d9_com *c,uint32_t format,uint32_t usage){
    uint32_t args[6]={0,1,c->factory.color_format,usage,1,format};
    return c->factory.query?c->factory.query(c->factory.context,0,args,6,NULL,0):NOT_AVAILABLE;
}
static void api_depth_match(void){
    struct fnv_d3d9_com *c=module();uint32_t args[6];for(unsigned i=0;i<6;++i)args[i]=fnv_cpu_argument_u32(i);
    fnv_host_mutex_lock(&c->guard);factory_this(c,args[0]);uint32_t hr=NOT_AVAILABLE;
    if(args[1])hr=FNV_D3D9_INVALIDCALL;
    else if(args[2]==1 && args[3]==c->factory.color_format && args[4]==21 && args[5]==75){
        hr=factory_surface_format(c,args[4],1);if(!hr)hr=factory_surface_format(c,args[5],2);
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,24);
}
static void api_multisample(void){
    struct fnv_d3d9_com *c=module();uint32_t args[7];for(unsigned i=0;i<7;++i)args[i]=fnv_cpu_argument_u32(i);
    unsigned char *out=args[6]?range(c,args[6],4,1):NULL;
    fnv_host_mutex_lock(&c->guard);factory_this(c,args[0]);uint32_t hr=NOT_AVAILABLE;
    if(args[1] || args[5]>16 || (args[6] && !out))hr=FNV_D3D9_INVALIDCALL;
    else if(args[2]==1 && !args[5] && (args[3]==21 || args[3]==75)){
        hr=factory_surface_format(c,args[3],args[3]==75?2:1);if(!hr && out)write32(out,1);
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,28);
}
static void api_adapter_monitor(void){unavailable(2,1);}
static void api_create_device(void){
    uint32_t self=fnv_cpu_argument_u32(0),adapter=fnv_cpu_argument_u32(1),type=fnv_cpu_argument_u32(2),focus=fnv_cpu_argument_u32(3),behavior=fnv_cpu_argument_u32(4),at=fnv_cpu_argument_u32(5),out_at=fnv_cpu_argument_u32(6);
    struct fnv_d3d9_com *c=module();unsigned char *params=range(c,at,56,1),*out=range(c,out_at,4,1);uint32_t values[14],result=FNV_D3D9_INVALIDCALL;
    if(!params || !out || ((uint64_t)at<(uint64_t)out_at+4 && (uint64_t)out_at<(uint64_t)at+56)){fnv_cpu_return_u32(result,28);return;}
    for(unsigned i=0;i<14;++i)values[i]=read32(params+i*4);
    fnv_host_mutex_lock(&c->guard);factory_this(c,self);write32(out,0);
    if(adapter || c->factory_created || !c->device_references)goto done;
    if(type!=1){result=NOT_AVAILABLE;goto done;}
    values[8]=values[8]!=0;values[9]=values[9]!=0;
    if((behavior&~(2u|4u|64u)) || !(behavior&64u) || (!focus && !values[7]) || (!values[8] && !focus))goto done;
    if(values[8]){if(!values[0])values[0]=c->factory.width;if(!values[1])values[1]=c->factory.height;if(!values[2])values[2]=c->factory.color_format;}
    if(!values[3])values[3]=1;
    if(values[0]!=c->factory.width || values[1]!=c->factory.height || values[2]!=c->factory.color_format || values[3]!=1 || values[4] || values[5] || values[6]!=1 ||
       (values[9]!=0)!=(c->factory.depth_format!=0) || (values[9] && values[10]!=c->factory.depth_format) || (values[11]&~2u) ||
       (values[8] && values[12]) || (!values[8] && values[12] && values[12]!=c->factory.refresh_hz) || values[13]>1){result=NOT_AVAILABLE;goto done;}
    c->factory_created=1;c->creation[0]=adapter;c->creation[1]=type;c->creation[2]=focus;c->creation[3]=behavior;
    c->config.presentation_window=values[7]?values[7]:focus;
    c->creator=(uintptr_t)&creator_marker;memcpy(c->present_params,values,sizeof(values));
    for(unsigned i=0;i<14;++i)write32(params+i*4,values[i]);
    write32(out,device_address(c));result=0;
done:fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,28);
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
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
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
