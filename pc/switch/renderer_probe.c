/* Own synthetic x86 callers -> COM -> native Vulkan/NVK. No game files/code. */
#include <switch.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "d3d9_vk_horizon.h"
#include "renderer_probe_assets.h"
#define BASE 0x400000u
#define BYTES (4u<<20)
#define DATA (BASE+0xa000)
#define OUT (BASE+0x9000)
static FILE *log_file;
static unsigned char *arena;
static struct fnv_vk_backend *gpu;
static struct fnv_cpu_runtime *cpu;
static struct fnv_d3d9_com *com;
static struct fnv_d3d9_shader_backend provider;
static uint32_t device,failed,attached;
static uint64_t calls,presents;

static void cpu_diagnostic(void *context,const struct fnv_cpu_fault *fault){
    (void)context;fprintf(log_file,"CPU fault kind=%u entry=%08lx address=%08lx detail=%08lx %s\n",(unsigned)fault->kind,(unsigned long)fault->entry,(unsigned long)fault->address,(unsigned long)fault->detail,fault->text);fflush(log_file);
}
static unsigned char *ptr(uint32_t at){return arena+at-BASE;}
static void put(uint32_t at,uint32_t v){memcpy(ptr(at),&v,4);}
static uint32_t get(uint32_t at){uint32_t v;memcpy(&v,ptr(at),4);return v;}
static int check(uint32_t hr,const char *step){
    if(hr){fprintf(log_file,"FAIL %s hr=%08lx\n",step,(unsigned long)hr);fflush(log_file);failed=1;return 0;
    }return 1;
}
static uint32_t call(unsigned method,unsigned count,const uint32_t *args){
    if(failed)return FNV_VK_LOST;
    struct fnv_cpu_state s,after;fnv_cpu_state_init(&s,BASE+BYTES-4096,0);
    put(s.gpr[4],BASE+0x1000);for(unsigned i=0;i<count;++i)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;
    if(!check(fnv_cpu_set_state(&s),"CPU set state") || !check(fnv_cpu_execute(0x401500+method*64,&fault),"translated COM call"))return FNV_VK_LOST;
    if(!check(fnv_cpu_get_state(&after),"CPU get state"))return FNV_VK_LOST;
    if(after.gpr[4]!=s.gpr[4]+4+count*4){check(FNV_VK_INVALID,"stdcall stack");return FNV_VK_INVALID;}
    calls++;return after.gpr[0];
}
static void constant(unsigned method,unsigned reg,const void *data){memcpy(ptr(DATA),data,16);uint32_t a[]={device,reg,DATA,1};check(call(method,4,a),"constant");}
static uint32_t create_shader(unsigned stage){
    const uint32_t *code=stage?probe_ps:probe_vs;uint32_t bytes=stage?sizeof(probe_ps):sizeof(probe_vs);
    memcpy(ptr(DATA),code,bytes);uint32_t args[]={device,DATA,OUT};
    if(!check(call(stage?6:5,3,args),"CreateShader"))return 0;
    return get(OUT);
}
#include "renderer_probe_checks.h"
static struct fnv_probe_scene scene;
static struct fnv_probe_checks combined;
static uint32_t probe_factory_query(void *ctx,uint32_t kind,const uint32_t *args,uint32_t count,void *output,uint32_t bytes){
    (void)output;(void)bytes;return kind==0 && count==6?fnv_vk_backend_check_format(ctx,args[3],args[4],args[5]):0x8876086au;
}
static int setup(void){
    VkResult reason;struct fnv_vk_config cfg={1280,720,32,2,8,64u<<20,1};
    fprintf(log_file,"create NVK/NWindow 1280x720 FIFO; watchdog=2000ms\n");fflush(log_file);
    uint32_t hr=fnv_vk_backend_create_horizon(vkGetInstanceProcAddr,&cfg,nwindowGetDefault(),1,&gpu,&reason);
    fprintf(log_file,"create hr=%08lx VkResult=%ld\n",(unsigned long)hr,(long)reason);fflush(log_file);
    if(!check(hr,"NVK create"))return 0;
    
    if(!check(fnv_vk_backend_draw_wait(gpu,2000000000ull),"draw watchdog"))return 0;
    fnv_vk_backend_callbacks(gpu,&provider);
    struct fnv_uniform_entry ve[4]={{0,7,1,0,16},{0,9,1,16,16},{1,2,1,32,16},{2,3,1,48,16}},pe[2]={{0,3,1,0,16},{0,12,1,16,16}};
    struct fnv_uniform_plan vp={64,4,ve},pp={32,2,pe};
    if(!check(fnv_vk_backend_register_shader(gpu,FNV_D3D9_VERTEX,probe_vs,sizeof(probe_vs),probe_vs_spv,sizeof(probe_vs_spv),&vp),"VS catalog") ||
       !check(fnv_vk_backend_register_shader(gpu,FNV_D3D9_PIXEL,probe_ps,sizeof(probe_ps),probe_ps_spv,sizeof(probe_ps_spv),&pp),"PS catalog"))return 0;
       
    const struct fnv_vk_vertex_input map[2]={{0,0,0},{5,0,1}};
    if(!check(fnv_vk_backend_link_program(gpu,probe_vs,sizeof(probe_vs),probe_ps,sizeof(probe_ps),map,2,1),"shader linkage"))return 0;
    
    arena=memalign(4096,BYTES);
    if(!arena){check(FNV_VK_OOM,"guest arena");return 0;
    }memset(arena,0,BYTES);
    struct fnv_cpu_config cc={.memory=arena,.guest_base=BASE,.memory_bytes=BYTES,.code_low=BASE+0x1000,.code_high=BASE+0x3000,.diagnostic=cpu_diagnostic};
    if(!check(fnv_cpu_create(&cc,&cpu),"CPU create"))return 0;
    
    const struct fnv_d3d9_shader_code catalog[2]={{probe_vs,sizeof(probe_vs)},{probe_ps,sizeof(probe_ps)}};
    struct fnv_d3d9_factory_profile profile={1280,720,21,75,60,gpu,probe_factory_query};
    struct fnv_d3d9_com_config dc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.catalog=catalog,.catalog_count=2,
        .backend=provider,.factory=&profile,.reset_timeout_ns=2000000000ull,.buffer_address=BASE+0x100000,.buffer_bytes=65536,.presentation_timeout_ns=2000000000ull};
    if(!check(fnv_d3d9_com_create(cpu,&dc,&com),"COM create"))return 0;
    
    device=fnv_d3d9_com_device(com);struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+BYTES-4096,0);
    if(!check(fnv_cpu_attach(cpu,&s),"CPU attach"))return 0;
    
    attached=1;
    uint32_t parent[]={device,OUT};
    if(!check(call(74,2,parent),"GetDirect3D prepared device"))return 0;
    uint32_t factory=get(OUT);
    const uint32_t params[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};memcpy(ptr(DATA),params,56);uint32_t create[]={factory,0,1,0x1234,0x46,DATA,OUT};
    if(!check(call(73,7,create),"prepared CreateDevice")||get(OUT)!=device)return 0;
    (void)call(2,1,&factory);
    uint32_t vs=create_shader(0),ps=create_shader(1),bind[]={device,vs};check(call(53,2,bind),"bind VS");bind[1]=ps;check(call(55,2,bind),"bind PS");
    if(failed)return 0;
    
    const unsigned char elements[24]={0,0,0,0,1,0,0,0,0,0,8,0,1,0,5,0,255,0,0,0,17,0,0,0};memcpy(ptr(DATA),elements,24);
    uint32_t decl[]={device,DATA,OUT};check(call(57,3,decl),"create declaration");bind[1]=get(OUT);scene.declaration=bind[1];check(call(58,2,bind),"bind declaration");
    if(failed)return 0;
    
    uint32_t vbargs[]={device,96,0,0,1,OUT,0};check(call(37,7,vbargs),"create VB");uint32_t vb=get(OUT);
    uint32_t ibargs[]={device,16,0,101,1,OUT,0};check(call(38,7,ibargs),"create IB");uint32_t ib=get(OUT);
    if(failed)return 0;
    
    const float positions[16]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,-1,0,1};const uint16_t indices[8]={0,0,1,2,3,1,3,4};
    uint32_t lock[]={vb,0,96,OUT,0};
    if(!check(call(39,5,lock),"lock VB"))return 0;
    
    memset(ptr(get(OUT)),0,96);memcpy(ptr(get(OUT))+16,positions,sizeof(positions));check(call(40,1,&vb),"unlock VB");
    lock[0]=ib;lock[2]=16;
    if(!check(call(39,5,lock),"lock IB"))return 0;
    
    memcpy(ptr(get(OUT)),indices,sizeof(indices));check(call(40,1,&ib),"unlock IB");
    uint32_t stream[]={device,0,vb,16,16},index[]={device,ib};check(call(41,5,stream),"bind VB");check(call(42,2,index),"bind IB");
    uint32_t texargs[]={device,2,2,1,0,21,1,OUT,0};check(call(46,9,texargs),"create texture");uint32_t tex=get(OUT);
    if(failed)return 0;
    
    uint32_t tl[]={tex,0,OUT,0,0};
    if(!check(call(47,5,tl),"lock texture"))return 0;
    
    uint32_t pitch=get(OUT),address=get(OUT+4);const unsigned gray[4]={255,128,64,192};
    if(!failed)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=ptr(address)+y*pitch+x*4;p[0]=p[1]=p[2]=(unsigned char)gray[y*2+x];p[3]=255;}
    uint32_t tu[]={tex,0};check(call(48,2,tu),"unlock texture");uint32_t tb[]={device,0,tex};check(call(49,3,tb),"bind texture");
    const float depth[4]={0,0,.6875f,0},adjust[4]={0,0,.0625f,0},bias[4]={0,0,0,1};const uint32_t ints[4]={2,0,0,0},boolean[4]={1,0,0,0};
    constant(7,7,depth);constant(7,9,adjust);constant(9,2,ints);constant(11,3,boolean);constant(13,3,bias);
    uint32_t state[]={device,22,1};check(call(33,3,state),"cull none");
    if(failed)return 0;
    
    scene.vs=vs;scene.ps=ps;scene.vb=vb;scene.ib=ib;scene.texture=tex;
    struct fnv_d3d9_state_value entry={.kind=FNV_D3D9_TEXTURE_BINDING};
    if(!check(provider.state_batch(gpu,&entry,1,1),"base texture ID"))return 0;
    
    uintptr_t id=(uintptr_t)((uint64_t)entry.values[0]|(uint64_t)entry.values[1]<<32);unsigned char ready[16];
    if(!check(fnv_vk_backend_read_texture(gpu,id,0,ready,16,2000000000ull),"base lazy texture upload"))return 0;
    
    combined=(struct fnv_probe_checks){.gpu=gpu,.provider=&provider,.device=device,.data=DATA,.out=OUT,.log=log_file,.call=call,.memory=ptr,.check=check};
    /* Keep a visible blue frame while the offscreen checks run. */
    uint32_t startup_clear[]={device,0,0,7,0xff102080,0x3f800000,0},startup_present[]={device,0,0,0,0};
    if(!check(call(27,7,startup_clear),"startup Clear")||!check(call(81,5,startup_present),"startup Present")||!check(fnv_vk_backend_wait(gpu,2000000000ull),"startup retirement"))return 0;
    
    presents++;
    if(!fnv_renderer_probe_checks(&combined,&scene)){failed=1;return 0;
    }
    const float opaque_bias[4]={0,0,0,1};constant(13,3,opaque_bias);
    return check(fnv_vk_backend_wait(gpu,2000000000ull),"setup completion");
}
static void readback(unsigned phase,unsigned channel){
    uintptr_t back;uint32_t hr=provider.get_target(gpu,0,0,&back);
    if(!check(hr,"readback target"))return;
    provider.release(gpu,back);
    size_t bytes=1280u*720u*4;unsigned char *pixels=malloc(bytes);
    if(!pixels){check(FNV_VK_OOM,"readback allocation");return;}
    if(check(fnv_vk_backend_read_color(gpu,back,pixels,bytes,2000000000ull),"GPU readback")){
        const unsigned gray[4]={255,128,64,192};unsigned errors=0;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=pixels+(((y?540:180)*1280)+(x?960:320))*4;
            unsigned char expected[4]={0,0,0,255};
            if(!phase){expected[0]=128;expected[1]=32;expected[2]=16;}else expected[2-channel]=(unsigned char)gray[y*2+x];
            if(memcmp(p,expected,4)){errors++;fprintf(log_file,"pixel phase=%u xy=%u,%u got=%u,%u,%u,%u expected=%u,%u,%u,%u\n",phase,x,y,p[0],p[1],p[2],p[3],expected[0],expected[1],expected[2],expected[3]);}}
        fprintf(log_file,"phase=%u GPU pixel checks=4 errors=%u\n",phase,errors);
        if(errors)failed=1;
        fflush(log_file);
    }
    free(pixels);
}
int main(void){
    mkdir("sdmc:/switch/FNV-nx",0777);log_file=fopen("sdmc:/switch/FNV-nx/fnv-pc-renderer-hw03.log","w");
    if(!log_file)return 1;
    setvbuf(log_file,NULL,_IOLBF,0);fprintf(log_file,"FNV pc-renderer-hw03; synthetic renderer only; no Fallout files/code\n");
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    int ready=setup();unsigned frame=0,last_phase=99;
    while(ready && !failed && appletMainLoop()){
        padUpdate(&pad);
        if(padGetButtonsDown(&pad)&HidNpadButton_Plus)break;
        unsigned phase=frame<180?frame/45:3,channel=phase?phase-1:0;
        if(phase!=last_phase){fprintf(log_file,"phase=%u begin frame=%u\n",phase,frame);last_phase=phase;}
        if(!check(fnv_vk_backend_wait(gpu,2000000000ull),"frame retirement"))break;
        uint32_t clear[]={device,0,0,7,phase?0xff000000:0xff102080,0x3f800000,0};check(call(27,7,clear),"Clear");
        if(phase){float tint[4]={0,0,0,0};tint[channel]=1;constant(13,12,tint);check(call(35,1,&device),"BeginScene");
            uint32_t draw[]={device,4,UINT32_MAX,1,4,2,2};check(call(61,7,draw),"DrawIndexedPrimitive");check(call(36,1,&device),"EndScene");}
        if(frame%45==0 && frame<180)readback(phase,channel);
        uint32_t present[]={device,0,0,0,0};
        if(!check(call(81,5,present),"Present COM"))break;
        presents++;frame++;
        if(frame==180 || frame%600==0){struct fnv_vk_stats s;fnv_vk_backend_stats(gpu,&s);fprintf(log_file,"frames=%u draws=%llu submits=%llu completions=%llu memory=%llu presents=%llu images=%lu mode=%lu; plus exits\n",frame,(unsigned long long)s.draws,(unsigned long long)s.submissions,(unsigned long long)s.completions,(unsigned long long)s.allocated_bytes,(unsigned long long)s.presentation_requests,(unsigned long)s.swapchain_images,(unsigned long)s.present_mode);}
    }
    if(gpu && !failed)check(fnv_vk_backend_wait(gpu,2000000000ull),"final retirement");
    if(attached)check(fnv_cpu_detach(),"CPU detach");
    if(com)check(fnv_d3d9_com_destroy(com),"COM destroy");else if(gpu)provider.device_release(gpu);
    if(cpu)check(fnv_cpu_destroy(cpu),"CPU destroy");
    free(arena);
    uint32_t destroyed=0;
    if(gpu){destroyed=FNV_VK_BUSY;for(unsigned i=0;i<100 && destroyed==FNV_VK_BUSY;++i){destroyed=fnv_vk_backend_destroy(gpu);
    if(destroyed==FNV_VK_BUSY)svcSleepThread(1000000);}}
    fprintf(log_file,"END result=%s calls=%llu presents=%llu destroy=%08lx\n",failed||destroyed?"FAIL":frame>=180?"PASS":"EARLY_EXIT",(unsigned long long)calls,(unsigned long long)presents,(unsigned long)destroyed);fclose(log_file);return 0;
    
}
