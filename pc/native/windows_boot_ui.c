/* Bounded native borderless window lifecycle and guest creation callbacks. */
#include "fnv_sdk_runtime.h"
#include "windows_boot_ui.h"
#include "windows_thread.h"
#include "windows_user32.h"
#include "guest_memory.h"
#include "pe_resources.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct icon_record {uint32_t handle;uint16_t id;struct fnv_pe_resource group;};
#define UI_EXPORTS 8
struct boot_window {uint32_t hwnd,atom,procedure,owner,phase;void *native;};
struct fnv_windows_boot_ui {
    struct fnv_cpu_runtime *cpu;struct fnv_native_image image;fnv_host_mutex guard;
    struct fnv_windows_user32 *desktop;struct fnv_boot_window_backend backend;
    struct boot_window windows[16];
    uint32_t tokens[UI_EXPORTS],next_atom;struct icon_record icons[16];struct fnv_boot_class classes[32];
};
static struct fnv_windows_boot_ui *active;
static uint16_t read_u16(const unsigned char *p){return p[0]|(uint16_t)p[1]<<8;}
static uint32_t read_u32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned char *checked(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(vm,&backing,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))p=NULL;}
    if(!p){recomp_unimpl("Windows UI memory unavailable",at);return NULL;}return p;
}
static int ready(void){if(active && active->cpu==fnv_cpu_current())return 1;recomp_unimpl("Windows UI provider unavailable",0);return 0;}
static int name(uint32_t at,char out[256]){
    if(at<=65535){recomp_unimpl("Windows UI class name shape pending",at);return 0;}
    for(uint32_t i=0;i<256;i++){
        if((uint64_t)at+i>UINT32_MAX){recomp_unimpl("Windows UI name wraps",at);return 0;}
        unsigned char *p=checked(at+i,1,0);if(!p)return 0;
        if(*p>127){recomp_unimpl("Windows UI name encoding pending",at);return 0;}
        out[i]=(char)*p;if(!*p){if(i)return 1;recomp_unimpl("Windows UI empty class name pending",at);return 0;}
    }recomp_unimpl("Windows UI name length pending",at);return 0;
}
static int equal(const char *a,const char *b){for(;;a++,b++){unsigned x=(unsigned char)*a,y=(unsigned char)*b;
    if(x>='A' && x<='Z')x+=32;
    if(y>='A' && y<='Z')y+=32;
    if(x!=y)return 0;
    if(!x)return 1;}}
static void api_icon(void){
    uint32_t instance=fnv_cpu_argument_u32(0),id=fnv_cpu_argument_u32(1);if(!ready())return;
    if(instance!=active->image.base || !id || id>65535){recomp_unimpl("Windows icon module/name variant pending",id);return;}
    struct fnv_pe_resource group;enum fnv_resource_result r=fnv_pe_resource_find(&active->image,14,(uint16_t)id,0x409,&group);
    if(r==FNV_RESOURCE_ABSENT){fnv_windows_set_last_error(1814);fnv_cpu_return_u32(0,8);return;}
    if(r!=FNV_RESOURCE_OK || group.bytes<6 || read_u16(group.data)!=0 || read_u16(group.data+2)!=1 || !read_u16(group.data+4) ||
       read_u16(group.data+4)>64 || group.bytes!=6u+14u*read_u16(group.data+4)){
        recomp_unimpl("Windows icon group format pending",id);return;}
    for(unsigned i=0;i<read_u16(group.data+4);i++){
        const unsigned char *entry=group.data+6+i*14;struct fnv_pe_resource icon;
        if(fnv_pe_resource_find(&active->image,3,read_u16(entry+12),group.language,&icon)!=FNV_RESOURCE_OK || icon.bytes!=read_u32(entry+8)){
            recomp_unimpl("Windows icon payload unavailable",id);return;}
    }
    uint32_t handle=0;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<16;i++)if(active->icons[i].handle && active->icons[i].id==id)handle=active->icons[i].handle;
    if(!handle)for(unsigned i=0;i<16;i++)if(!active->icons[i].handle){handle=0xa1000000u+i*4;active->icons[i]=(struct icon_record){handle,(uint16_t)id,group};break;}
    fnv_host_mutex_unlock(&active->guard);
    if(!handle)fnv_windows_set_last_error(8);
    fnv_cpu_return_u32(handle,8);
}
static void api_stock(void){uint32_t id=fnv_cpu_argument_u32(0);if(!ready())return;
    if(id!=4){recomp_unimpl("Windows stock object variant pending",id);return;}
    fnv_cpu_return_u32(0xa2000004u,4); /* Typed black solid-brush identity. */
}
static void api_register(void){
    uint32_t at=fnv_cpu_argument_u32(0);if(!ready())return;unsigned char *p=checked(at,40,0);if(!p)return;
    struct fnv_boot_class c={0};c.style=read_u32(p);c.procedure=read_u32(p+4);c.instance=read_u32(p+16);c.icon=read_u32(p+20);c.brush=read_u32(p+28);
    if((c.style&~3u) || !c.procedure || !RECOMP_ICALL_IS_CODE(c.procedure) || (!recomp_lookup(c.procedure) && !recomp_lookup_kernel(c.procedure)) ||
       read_u32(p+8) || read_u32(p+12) || c.instance!=active->image.base || read_u32(p+24) || read_u32(p+32) || (c.brush && c.brush!=0xa2000004u)){
        recomp_unimpl("Windows class attributes/procedure pending",at);return;}
    if(!name(read_u32(p+36),c.name))return;
    uint32_t error=0;fnv_host_mutex_lock(&active->guard);int known=!c.icon;
    for(unsigned i=0;i<16;i++)if(active->icons[i].handle && active->icons[i].handle==c.icon)known=1;
    int slot=-1;for(unsigned i=0;i<32;i++){
        if(!active->classes[i].atom){if(slot<0)slot=(int)i;}
        else if(equal(active->classes[i].name,c.name))error=1410;
    }
    if(!known)error=6;
    if(!error){if(slot<0 || active->next_atom>65535)error=8;else {c.atom=active->next_atom++;active->classes[slot]=c;}}
    fnv_host_mutex_unlock(&active->guard);
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(error?0:c.atom,4);
}
static void api_unregister(void){
    uint32_t at=fnv_cpu_argument_u32(0),instance=fnv_cpu_argument_u32(1);if(!ready())return;char text[256];
    if(at>65535 && !name(at,text))return;
    uint32_t value=0,error=1411;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<32;i++){struct fnv_boot_class *c=active->classes+i;
        if(c->atom && c->instance==instance && (at<=65535?c->atom==at:equal(c->name,text))){
            int used=0;for(unsigned j=0;j<16;j++)if(active->windows[j].phase && active->windows[j].atom==c->atom)used=1;
            if(used)error=1412;else {memset(c,0,sizeof(*c));value=1;}break;}}
    fnv_host_mutex_unlock(&active->guard);if(!value)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
static void api_adjust(void){
    uint32_t at=fnv_cpu_argument_u32(0),style=fnv_cpu_argument_u32(1),menu=fnv_cpu_argument_u32(2);if(!ready())return;
    if((style&~0x90000000u) || menu){recomp_unimpl("Windows decorated rectangle pending",style);return;}
    if(!checked(at,16,1))return;
    fnv_cpu_return_u32(1,12); /* Borderless: client == outer. */
}
static void write_u32(unsigned char *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static uint32_t message(struct boot_window *w,uint32_t msg,uint32_t wp,uint32_t lp){
    struct fnv_cpu_state state;uint32_t low,high,id;
    fnv_windows_current_stack_bounds(&low,&high,&id);
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || id!=w->owner || (state.gpr[4]&3u) || state.gpr[4]>high ||
       state.gpr[4]<low || state.gpr[4]-low<160){recomp_unimpl("Windows callback stack/owner invalid",w->hwnd);return 0;}
    uint32_t frame=(state.gpr[4]-160)&~3u;unsigned char *p=checked(frame,20,1);if(!p)return 0;
    /* Actual native-import continuation, never a host address. */
    unsigned char *ret=checked(state.gpr[4],4,0);if(!ret)return 0;
    write_u32(p,read_u32(ret));write_u32(p+4,w->hwnd);write_u32(p+8,msg);write_u32(p+12,wp);write_u32(p+16,lp);
    return fnv_cpu_invoke_stdcall(w->procedure,frame,16);
}
static void release_window(struct fnv_windows_boot_ui *u,struct boot_window *w){
    if(w->native)u->backend.destroy(u->backend.context,w->native);
    if(w->hwnd)(void)fnv_windows_user32_remove_window(u->desktop,w->hwnd);
    fnv_host_mutex_lock(&u->guard);memset(w,0,sizeof(*w));fnv_host_mutex_unlock(&u->guard);
}
static void api_create_window(void){
    if(!ready())return;
    uint32_t a[12];for(unsigned i=0;i<12;i++)a[i]=fnv_cpu_argument_u32(i);
    if(a[0] || (a[3]&~0x90000000u) || a[4] || a[5] || !a[6] || a[6]>65535 || !a[7] || a[7]>65535 ||
       a[8] || a[9] || a[10]!=active->image.base){recomp_unimpl("Windows window attributes pending",a[3]);return;}
    char classname[256],title[256]={0};
    if((a[1]>65535 && !name(a[1],classname)) || (a[2] && !name(a[2],title)))return;
    if(!active->desktop || !active->backend.create){recomp_unimpl("Windows presentation backend unavailable",0);return;}
    uint32_t low,high,id;struct fnv_cpu_state state;fnv_windows_current_stack_bounds(&low,&high,&id);
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || (state.gpr[4]&3u) || state.gpr[4]>high || state.gpr[4]<low || state.gpr[4]-low<160){
        recomp_unimpl("Windows creation stack unavailable",0);return;}
    uint32_t cs=(state.gpr[4]-96)&~3u;unsigned char *data=checked(cs,48,1);if(!data)return;
    struct fnv_boot_class cls={0};struct boot_window *w=NULL;
    fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<32;i++){struct fnv_boot_class *c=active->classes+i;
        if(c->atom && (a[1]<=65535?c->atom==a[1]:equal(c->name,classname))){cls=*c;break;}}
    if(cls.atom)for(unsigned i=0;i<16;i++)if(!active->windows[i].phase){w=active->windows+i;*w=(struct boot_window){0,cls.atom,cls.procedure,id,1,NULL};break;}
    fnv_host_mutex_unlock(&active->guard);
    if(!cls.atom || !w){fnv_windows_set_last_error(cls.atom?8:1407);fnv_cpu_return_u32(0,48);return;}
    struct fnv_boot_window_request request={a[6],a[7],a[3],0,0,title};void *native=NULL;
    if(!active->backend.create(active->backend.context,&request,&native) || !native){
        if(native)active->backend.destroy(active->backend.context,native);
        release_window(active,w);fnv_windows_set_last_error(8);fnv_cpu_return_u32(0,48);return;}
    fnv_host_mutex_lock(&active->guard);w->native=native;fnv_host_mutex_unlock(&active->guard);
    uint32_t handle=0;
    if(fnv_windows_user32_add_window(active->desktop,cls.name,title,&handle)!=FNV_CPU_OK){release_window(active,w);fnv_windows_set_last_error(8);fnv_cpu_return_u32(0,48);return;}
    fnv_host_mutex_lock(&active->guard);w->hwnd=handle;fnv_host_mutex_unlock(&active->guard);
    /* CREATESTRUCTA32: param, instance, menu, parent, cy,cx,y,x,style,name,class,exstyle. */
    uint32_t fields[]={a[11],a[10],a[9],a[8],a[7],a[6],a[5],a[4],a[3],a[2],a[1],a[0]};
    for(unsigned i=0;i<12;i++)write_u32(data+i*4,fields[i]);
    if(!message(w,0x81,0,cs)){
        (void)message(w,0x82,0,0);release_window(active,w);fnv_cpu_return_u32(0,48);return;}
    if(message(w,1,0,cs)==UINT32_MAX){
        (void)message(w,2,0,0);(void)message(w,0x82,0,0);release_window(active,w);fnv_cpu_return_u32(0,48);return;}
    fnv_host_mutex_lock(&active->guard);w->phase=2;fnv_host_mutex_unlock(&active->guard);
    fnv_cpu_return_u32(w->hwnd,48);
}
static void api_def_window(void){
    uint32_t hwnd=fnv_cpu_argument_u32(0),msg=fnv_cpu_argument_u32(1);if(!ready())return;
    int known=0;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<16;i++)if(active->windows[i].phase && active->windows[i].hwnd==hwnd)known=1;
    fnv_host_mutex_unlock(&active->guard);
    if(!known){fnv_windows_set_last_error(1400);fnv_cpu_return_u32(0,16);return;}
    if(msg!=0x81 && msg!=1 && msg!=2 && msg!=0x82){recomp_unimpl("Windows default message pending",msg);return;}
    fnv_cpu_return_u32(msg==0x81,16);
}
static void api_destroy_window(void){
    uint32_t hwnd=fnv_cpu_argument_u32(0),low,high,id;if(!ready())return;
    fnv_windows_current_stack_bounds(&low,&high,&id);(void)low;(void)high;
    struct boot_window *w=NULL;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<16;i++)if(active->windows[i].hwnd==hwnd && active->windows[i].phase==2 && active->windows[i].owner==id){w=active->windows+i;w->phase=3;break;}
    fnv_host_mutex_unlock(&active->guard);
    if(!w){fnv_windows_set_last_error(1400);fnv_cpu_return_u32(0,4);return;}
    (void)message(w,2,0,0);(void)message(w,0x82,0,0);release_window(active,w);fnv_cpu_return_u32(1,4);
}
static const struct fnv_cpu_export exports[]={
    {"user32.dll","LoadIconA",0,0,api_icon},{"gdi32.dll","GetStockObject",0,0,api_stock},
    {"user32.dll","RegisterClassA",0,0,api_register},{"user32.dll","UnregisterClassA",0,0,api_unregister},
    {"user32.dll","AdjustWindowRect",0,0,api_adjust},
    {"user32.dll","CreateWindowExA",0,0,api_create_window},{"user32.dll","DefWindowProcA",0,0,api_def_window},
    {"user32.dll","DestroyWindow",0,0,api_destroy_window}
};
const struct fnv_cpu_export *fnv_windows_boot_ui_exports(size_t *n){if(n)*n=UI_EXPORTS;return exports;}
enum fnv_cpu_result fnv_windows_boot_ui_create(struct fnv_windows_process *process,const struct fnv_native_image *image,struct fnv_windows_boot_ui **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!process || !image || !image->data)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    void *mapped=NULL;
    if(fnv_cpu_prepare_memory(fnv_windows_runtime(process),image->base,image->size,&mapped)!=FNV_CPU_OK || mapped!=image->data)return FNV_CPU_INVALID;
    struct fnv_windows_boot_ui *u=calloc(1,sizeof(*u));if(!u)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&u->guard)){free(u);return FNV_CPU_NO_MEMORY;}
    u->cpu=fnv_windows_runtime(process);u->image=*image;u->next_atom=0xc000;
    fnv_cpu_function functions[UI_EXPORTS]={api_icon,api_stock,api_register,api_unregister,api_adjust,api_create_window,api_def_window,api_destroy_window};
    enum fnv_cpu_result r=fnv_cpu_register_native(u->cpu,functions,UI_EXPORTS,u->tokens);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&u->guard);free(u);return r;}active=u;*out=u;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_boot_ui_destroy(struct fnv_windows_boot_ui *u){
    if(!u || active!=u)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(u->cpu,u->tokens[0],UI_EXPORTS);if(r!=FNV_CPU_OK)return r;
    for(unsigned i=0;i<16;i++)if(u->windows[i].phase)release_window(u,u->windows+i);
    active=NULL;fnv_host_mutex_destroy(&u->guard);free(u);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_boot_ui_class(struct fnv_windows_boot_ui *u,uint32_t atom,struct fnv_boot_class *out){
    if(!u || u!=active || !out)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=FNV_CPU_INVALID;
    fnv_host_mutex_lock(&u->guard);for(unsigned i=0;i<32;i++)if(u->classes[i].atom && u->classes[i].atom==atom){*out=u->classes[i];r=FNV_CPU_OK;break;}
    fnv_host_mutex_unlock(&u->guard);return r;
}

enum fnv_cpu_result fnv_windows_boot_ui_set_backend(struct fnv_windows_boot_ui *u,struct fnv_windows_user32 *desktop,const struct fnv_boot_window_backend *backend){
    if(!u || u!=active || !desktop || !backend || !backend->create || !backend->destroy)return FNV_CPU_INVALID;
    void *image=NULL;enum fnv_cpu_result r=fnv_cpu_prepare_memory(u->cpu,u->image.base,u->image.size,&image);
    if(r!=FNV_CPU_OK)return r;
    u->desktop=desktop;u->backend=*backend;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_boot_ui_presentation(struct fnv_windows_boot_ui *u,uint32_t hwnd,void **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!u || u!=active || fnv_cpu_current()!=u->cpu)return FNV_CPU_INVALID;
    uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)low;(void)high;
    fnv_host_mutex_lock(&u->guard);
    for(unsigned i=0;i<16;i++)if(u->windows[i].hwnd==hwnd && u->windows[i].phase && u->windows[i].owner==id)*out=u->windows[i].native;
    fnv_host_mutex_unlock(&u->guard);return *out?FNV_CPU_OK:FNV_CPU_INVALID;
}
