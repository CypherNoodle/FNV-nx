/* The identical checks run on desktop Vulkan and Horizon/NVK. No platform API. */
#ifndef FNV_RENDERER_PROBE_CHECKS_H
#define FNV_RENDERER_PROBE_CHECKS_H
struct fnv_probe_scene {uint32_t vs,ps,declaration,vb,ib,texture;};
struct fnv_probe_checks {
    struct fnv_vk_backend *gpu;struct fnv_d3d9_shader_backend *provider;
    uint32_t device,data,out;FILE *log;
    uint32_t (*call)(unsigned,unsigned,const uint32_t *);
    unsigned char *(*memory)(uint32_t);int (*check)(uint32_t,const char *);
    unsigned groups,pixels,draws;
    unsigned replace_render_source; /* Desktop negative control only. */
};
static struct fnv_probe_checks *hw;
static uint32_t hw_get(uint32_t address){uint32_t v;memcpy(&v,hw->memory(address),4);return v;}
static int hw_call(unsigned method,unsigned count,const uint32_t *args,const char *step){return hw->check(hw->call(method,count,args),step);}
static void hw_group(const char *name){fprintf(hw->log,"combined BEGIN %s\n",name);fflush(hw->log);}
static int hw_release(uint32_t object){(void)hw->call(2,1,&object);return 1;}
static int hw_state(unsigned name,unsigned value){uint32_t a[]={hw->device,name,value};return hw_call(33,3,a,"SetRenderState");}
static int hw_bind(unsigned method,uint32_t object){uint32_t a[]={hw->device,object};return hw_call(method,2,a,"bind object");}
static int hw_texture(uint32_t object){uint32_t a[]={hw->device,0,object};return hw_call(49,3,a,"SetTexture");}
static int hw_constant(unsigned method,unsigned reg,const void *bits){memcpy(hw->memory(hw->data),bits,16);uint32_t a[]={hw->device,reg,hw->data,1};return hw_call(method,4,a,"shader constant");}
static int hw_constants(void){
    const float depth[4]={0,0,.4375f,0},adjust[4]={0,0,.0625f,0},zero[4]={0},one[4]={1,1,1,1};const uint32_t ints[4]={2,0,0,0},boolean[4]={1,0,0,0};
    return hw_constant(7,7,depth)&&hw_constant(7,9,adjust)&&hw_constant(9,2,ints)&&hw_constant(11,3,boolean)&&hw_constant(13,3,zero)&&hw_constant(13,12,one);
}
static int hw_restore(const struct fnv_probe_scene *s){
    uint32_t stream[]={hw->device,0,s->vb,16,16};
    return hw_bind(53,s->vs)&&hw_bind(55,s->ps)&&hw_bind(58,s->declaration)&&hw_call(41,5,stream,"restore stream")&&hw_bind(42,s->ib)&&hw_texture(s->texture)&&hw_constants()&&hw_state(7,0)&&hw_state(22,1)&&hw_state(27,0)&&hw_state(168,15)&&hw_state(174,0);
}
static int hw_pixels(unsigned r,unsigned g,unsigned b,unsigned alpha){
    uintptr_t target=0;
    if(!hw->check(hw->provider->get_target(hw->gpu,0,0,&target),"pixel target"))return 0;
    
    unsigned char pixels[4096];uint32_t hr=fnv_vk_backend_read_color(hw->gpu,target,pixels,sizeof(pixels),2000000000ull);hw->provider->release(hw->gpu,target);
    if(!hw->check(hr,"combined GPU pixels"))return 0;
    
    const unsigned char expected[4]={(unsigned char)b,(unsigned char)g,(unsigned char)r,(unsigned char)alpha};
    for(unsigned i=0;i<1024;++i)if(memcmp(pixels+i*4,expected,4)){fprintf(hw->log,"combined pixel %u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",i,pixels[i*4],pixels[i*4+1],pixels[i*4+2],pixels[i*4+3],b,g,r,alpha);fflush(hw->log);return hw->check(FNV_VK_INVALID,"combined pixel mismatch");}
    hw->pixels+=1024;return 1;
}
static int hw_clear(void){uint32_t a[]={hw->device,0,0,7,0xff000000,0x3f800000,0};return hw->check(fnv_vk_backend_wait(hw->gpu,2000000000ull),"draw retirement")&&hw_call(27,7,a,"Clear");}
static int hw_draw(unsigned kind){
    if(!hw_clear()||!hw_call(35,1,&hw->device,"BeginScene"))return 0;
    
    uint32_t indexed[]={hw->device,4,UINT32_MAX,1,4,2,2};
    uint32_t up[]={hw->device,4,2,hw->data+512,kind==3?20u:16u};
    uint32_t up_indexed[]={hw->device,4,0,4,2,hw->data+1024,101,hw->data+512,16};
    if(!hw_call(kind==0?61:kind==2?85:84,kind==0?7:kind==2?9:5,kind==0?indexed:kind==2?up_indexed:up,"combined Draw"))return 0;
    
    hw->draws++;return hw_call(36,1,&hw->device,"EndScene");
}
static int hw_targets(uint32_t,uint32_t);
/* Two ordered passes without a host wait or readback between them. */
static int hw_render_texture(uint32_t source,uint32_t color,uint32_t depth){
    hw_group("render texture/shared surface/container");
    struct fnv_vk_stats before,after;fnv_vk_backend_stats(hw->gpu,&before);
    uint32_t create[]={hw->device,32,32,1,1,21,0,hw->out,0};
    if(!hw_call(46,9,create,"CreateTexture render target"))return 0;
    uint32_t texture=hw_get(hw->out),level[]={texture,0,hw->out};
    if(!hw_call(103,3,level,"GetSurfaceLevel"))return 0;
    uint32_t surface=hw_get(hw->out);
    hw_release(texture);
    const unsigned char unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    memcpy(hw->memory(hw->data),unknown,16);uint32_t container[]={surface,hw->data,hw->out};
    if(!hw_call(104,3,container,"GetContainer")||hw_get(hw->out)!=texture)return hw->check(FNV_VK_INVALID,"render texture identity");
    const float first[4]={1,0,0,1},second[4]={1,1,1,1};
    uint32_t clear[]={hw->device,0,0,7,0xff000000,0x3f800000,0},draw[]={hw->device,4,UINT32_MAX,1,4,2,2};
    if(!hw_targets(surface,depth)||!hw_texture(source)||!hw_constant(13,12,first)||!hw_call(27,7,clear,"RT Clear")||!hw_call(35,1,&hw->device,"RT BeginScene")||!hw_call(61,7,draw,"RT Draw")||!hw_call(36,1,&hw->device,"RT EndScene"))return 0;
    hw->draws++;
    if(!hw_targets(color,depth)||!hw_texture(hw->replace_render_source?source:texture)||!hw_constant(13,12,second)||!hw_call(27,7,clear,"sample RT Clear")||!hw_call(35,1,&hw->device,"sample RT BeginScene")||!hw_call(61,7,draw,"sample RT Draw")||!hw_call(36,1,&hw->device,"sample RT EndScene"))return 0;
    hw->draws++;
    if(!hw_pixels(255,0,0,255)||!hw_texture(source)||!hw_constants())return 0;
    hw_release(surface);hw_release(texture);fnv_vk_backend_stats(hw->gpu,&after);
    if(after.allocated_bytes!=before.allocated_bytes||after.live_surfaces!=before.live_surfaces||after.live_textures!=before.live_textures||after.texture_versions!=before.texture_versions)return hw->check(FNV_VK_INVALID,"render texture release");
    hw->groups++;return 1;
}
static uint32_t hw_make_texture(unsigned format,unsigned levels,const unsigned char *block){
    uint32_t create[]={hw->device,4,4,levels,0,format,1,hw->out,0};
    if(!hw_call(46,9,create,"CreateTexture"))return 0;
    uint32_t texture=hw_get(hw->out);
    struct fnv_vk_stats before,after;fnv_vk_backend_stats(hw->gpu,&before);
    for(unsigned level=0;level<levels;++level){uint32_t lock[]={texture,level,hw->out,0,0};
    if(!hw_call(47,5,lock,"LockRect"))return 0;
    
        unsigned char *dst=hw->memory(hw_get(hw->out+4));unsigned pitch=hw_get(hw->out),w=4>>level;
        if(!w)w=1;
        if(format==21){for(unsigned y=0;y<w;++y)for(unsigned x=0;x<w;++x)memcpy(dst+y*pitch+x*4,block,4);}else memcpy(dst,block,format==0x31545844u?8:16);
        uint32_t unlock[]={texture,level};
        if(!hw_call(48,2,unlock,"UnlockRect"))return 0;
        
    }
    fnv_vk_backend_stats(hw->gpu,&after);
    if(after.submissions!=before.submissions){hw->check(FNV_VK_INVALID,"Unlock submitted GPU work");return 0;
    }
    return texture;
}
static int hw_ready(uint32_t texture,unsigned format,unsigned levels,const unsigned char *block){
    if(!hw_texture(texture))return 0;
    struct fnv_d3d9_state_value entry={.kind=FNV_D3D9_TEXTURE_BINDING};
    if(!hw->check(hw->provider->state_batch(hw->gpu,&entry,1,1),"texture binding capture"))return 0;
    
    uintptr_t id=(uintptr_t)((uint64_t)entry.values[0]|(uint64_t)entry.values[1]<<32);
    for(unsigned level=0;level<levels;++level){unsigned char bytes[64];unsigned n=format==21?64:format==0x31545844u?8:16;
        if(!hw->check(fnv_vk_backend_read_texture(hw->gpu,id,level,bytes,n,2000000000ull),"lazy texture first use"))return 0;
        
        if(format!=21 && memcmp(bytes,block,n))return hw->check(FNV_VK_INVALID,"compressed mip bits");
    }return 1;
}
static uint32_t hw_surface(int depth){uint32_t a[]={hw->device,32,32,depth?75:21,0,0,0,hw->out,0};return hw_call(depth?21:20,9,a,"CreateSurface")?hw_get(hw->out):0;}
static int hw_targets(uint32_t color,uint32_t depth){uint32_t rt[]={hw->device,0,color};return hw_bind(25,0)&&hw_call(23,3,rt,"SetRenderTarget")&&hw_bind(25,depth);}
static uint32_t hw_clone_shader(unsigned stage,uint32_t original){
    uint32_t function[]={original,0,hw->out};
    if(!hw_call(4,3,function,"shader bytecode size"))return 0;
    unsigned n=hw_get(hw->out);
    if(!n||n>4096){hw->check(FNV_VK_INVALID,"shader size");return 0;
    }function[1]=hw->data;
    if(!hw_call(4,3,function,"shader bytecode copy"))return 0;
    uint32_t create[]={hw->device,hw->data,hw->out};return hw_call(stage?6:5,3,create,"clone shader")?hw_get(hw->out):0;
}
static int fnv_renderer_probe_checks(struct fnv_probe_checks *checks,const struct fnv_probe_scene *scene){
    hw=checks;uint32_t rt_args[]={hw->device,0,hw->out},ds_args[]={hw->device,hw->out};
    if(!hw_call(24,3,rt_args,"GetRenderTarget"))return 0;
    uint32_t back=hw_get(hw->out);
    if(!hw_call(26,2,ds_args,"GetDepthStencilSurface"))return 0;
    uint32_t depth=hw_get(hw->out),small=hw_surface(0),small_depth=hw_surface(1);
    if(!small||!small_depth||!hw_targets(small,small_depth)||!hw_restore(scene))return 0;
    
    hw_group("clear");
    if(!hw_clear()||!hw_pixels(0,0,0,255))return 0;
    hw->groups++;
    const unsigned formats[3]={0x31545844u,0x33545844u,0x35545844u};
    for(unsigned f=0;f<3;++f){hw_group(f==0?"BC1/mips":f==1?"BC2/alpha":"BC3/alpha");unsigned char block[16]={0};unsigned char *color=block+(f?8:0);color[0]=0;color[1]=0xf8;
        if(f==1)memset(block,0x55,8);
        if(f==2)block[0]=128;
        uint32_t texture=hw_make_texture(formats[f],3,block);
        if(!texture||!hw_ready(texture,formats[f],3,block)||!hw_draw(0)||!hw_pixels(255,0,0,f==0?255:f==1?85:128))return 0;
        
        if(!hw_texture(scene->texture))return 0;
        hw_release(texture);hw->groups++;
    }
    const unsigned char white[4]={255,255,255,255};uint32_t texture=hw_make_texture(21,1,white);
    if(!texture||!hw_ready(texture,21,1,white))return 0;
    if(!hw_render_texture(texture,small,small_depth))return 0;
    
    const float triangles[24]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,1,0,0,1,-1,1,1,-1,-1,0,1};
    hw_group("DrawPrimitiveUP");memcpy(hw->memory(hw->data+512),triangles,sizeof(triangles));
    if(!hw_draw(1)||!hw_pixels(255,255,255,255))return 0;
    hw->groups++;
    const float corners[16]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,-1,0,1};const uint16_t indices[6]={0,1,2,0,2,3};
    hw_group("DrawIndexedPrimitiveUP");memcpy(hw->memory(hw->data+512),corners,sizeof(corners));memcpy(hw->memory(hw->data+1024),indices,sizeof(indices));
    if(!hw_draw(2)||!hw_pixels(255,255,255,255))return 0;
    
    uint32_t getter[]={hw->device,0,hw->out,hw->out+4,hw->out+8};
    if(!hw_call(44,5,getter,"UP stream cleared")||hw_get(hw->out))return hw->check(FNV_VK_INVALID,"UP stream binding");
    hw->groups++;
    hw_group("FVF XYZ/UV");uint32_t fvf[]={hw->device,0x102};
    if(!hw_call(86,2,fvf,"SetFVF"))return 0;
    
    uint32_t fvf_get[]={hw->device,hw->out};
    if(!hw_call(87,2,fvf_get,"GetFVF")||hw_get(hw->out)!=0x102)return hw->check(FNV_VK_INVALID,"FVF identity");
    float xyz[30];for(unsigned i=0;i<6;++i){xyz[i*5]=triangles[i*4];xyz[i*5+1]=triangles[i*4+1];xyz[i*5+2]=0;xyz[i*5+3]=triangles[i*4+2];xyz[i*5+4]=triangles[i*4+3];}memcpy(hw->memory(hw->data+512),xyz,sizeof(xyz));
    if(!hw_draw(3)||!hw_pixels(255,255,255,255)||!hw_restore(scene))return 0;
    hw->groups++;
    hw_group("state blocks/constants/textures/shaders");const unsigned char green[4]={0,255,0,255};uint32_t colored=hw_make_texture(21,1,green);
    if(!colored||!hw_ready(colored,21,1,green)||!hw_texture(scene->texture))return 0;
    
    uint32_t vs=hw_clone_shader(0,scene->vs),ps=hw_clone_shader(1,scene->ps);
    if(!vs||!ps||!hw_call(88,1,&hw->device,"BeginStateBlock")||!hw_state(168,0)||!hw_texture(colored)||!hw_bind(53,vs)||!hw_bind(55,ps)||!hw_constants())return 0;
    
    uint32_t end[]={hw->device,hw->out};
    if(!hw_call(89,2,end,"EndStateBlock"))return 0;
    uint32_t block=hw_get(hw->out);hw_release(vs);hw_release(ps);hw_release(colored);
    if(!hw_call(91,1,&block,"Apply zero mask")||!hw_draw(0)||!hw_pixels(0,0,0,255)||!hw_state(168,15)||!hw_call(90,1,&block,"Capture state")||!hw_restore(scene)||!hw_state(168,0)||!hw_call(91,1,&block,"Apply captured state")||!hw_draw(0)||!hw_pixels(0,255,0,255))return 0;
    
    hw_release(block);
    if(!hw_restore(scene)||!hw_targets(back,depth))return 0;
    hw_release(small);hw_release(small_depth);hw_release(back);hw_release(depth);hw_release(texture);hw->groups++;
    hw_group("Reset and restored shader/texture block");texture=hw_make_texture(21,1,white);
    if(!texture||!hw_ready(texture,21,1,white)||!hw_texture(scene->texture)||!hw_call(88,1,&hw->device,"BeginStateBlock")||!hw_texture(texture)||!hw_bind(53,scene->vs)||!hw_bind(55,scene->ps)||!hw_constants()||!hw_state(7,0)||!hw_state(22,1)||!hw_state(168,15)||!hw_call(89,2,end,"EndStateBlock Reset"))return 0;
    
    block=hw_get(hw->out);hw_release(texture);
    const uint32_t params[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};memcpy(hw->memory(hw->data),params,56);uint32_t reset[]={hw->device,hw->data};
    if(!hw_call(82,2,reset,"Reset")||!hw_call(83,1,&hw->device,"TestCooperativeLevel")||!hw_restore(scene)||!hw_call(91,1,&block,"Apply after Reset"))return 0;
    
    if(!hw_call(24,3,rt_args,"new backbuffer"))return 0;
    back=hw_get(hw->out);
    if(!hw_call(26,2,ds_args,"new depth"))return 0;
    depth=hw_get(hw->out);small=hw_surface(0);small_depth=hw_surface(1);
    if(!small||!small_depth||!hw_targets(small,small_depth)||!hw_draw(0)||!hw_pixels(255,255,255,255)||!hw_targets(back,depth)||!hw_restore(scene))return 0;
    
    hw_release(small);hw_release(small_depth);hw_release(back);hw_release(depth);hw_release(block);hw->groups++;
    fprintf(hw->log,"combined PASS groups=%u pixels=%u draws=%u\n",hw->groups,hw->pixels,hw->draws);fflush(hw->log);
    return hw->groups==10 && hw->pixels==11264 && hw->draws==11;
}
#endif
