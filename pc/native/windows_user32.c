/* Original local-console USER32 bridge, independent of the host desktop. */
#include "fnv_sdk_runtime.h"
#include "windows_user32.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct guest_window {uint32_t handle;char class_name[256],title[512];};
struct fnv_windows_user32 {
    struct fnv_cpu_runtime *cpu;fnv_host_mutex guard;uint32_t token,next;
    struct guest_window windows[16];struct fnv_user32_metrics metrics;
};
/* One virtual desktop, isolated from the PC host. Lifecycle matches the native
 * module catalog: create before attach, destroy after every thread detaches. */
static struct fnv_windows_user32 *active;
static int ascii_copy(char *out,size_t capacity,const char *in){
    if(!in)return 0;
    for(size_t i=0;i<capacity;i++){unsigned c=(unsigned char)in[i];if(c>127)return 0;
        out[i]=(char)c;if(!c)return 1;}
    return 0;
}
static int equal_folded(const char *a,const char *b){
    for(;;a++,b++){unsigned x=(unsigned char)*a,y=(unsigned char)*b;
        if(x>='A' && x<='Z')x+=32;
        if(y>='A' && y<='Z')y+=32;
        if(x!=y)return 0;
        if(!x)return 1;}
}
static int guest_string(uint32_t at,char *out,size_t capacity){
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    unsigned char *backing=NULL;uint32_t base=0,bytes=0;
    if(vm && !fnv_vm_binding(vm,&backing,&base,&bytes))return 0;
    for(size_t i=0;i<capacity;i++){
        uint64_t pos=(uint64_t)at+i;if(pos>UINT32_MAX)return 0;
        unsigned char *b=fnv_cpu_try_memory((uint32_t)pos,1);
        if(!b || (vm && pos>=base && pos<(uint64_t)base+bytes && !fnv_vm_access(vm,(uint32_t)pos,1,0)))return 0;
        if(*b>127)return 0;
        out[i]=(char)*b;if(!*b)return 1;
    }
    return 0;
}
static void api_find_window_a(void){
    uint32_t class_at=fnv_cpu_argument_u32(0),title_at=fnv_cpu_argument_u32(1),value=0;
    if(!active || active->cpu!=fnv_cpu_current()){
        recomp_unimpl("Windows virtual desktop unavailable",class_at);return;}
    char class_name[256],title[512];
    /* Class atoms require RegisterClass support; do not dereference an atom. */
    if((class_at && (class_at<=65535 || !guest_string(class_at,class_name,sizeof(class_name)))) ||
       (title_at && !guest_string(title_at,title,sizeof(title)))){
        recomp_unimpl("Windows FindWindowA string/atom variant pending",class_at?class_at:title_at);return;}
    fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<16;i++){struct guest_window *w=active->windows+i;
        if(w->handle && (!class_at || equal_folded(class_name,w->class_name)) &&
           (!title_at || equal_folded(title,w->title))){value=w->handle;break;}}
    fnv_host_mutex_unlock(&active->guard);
    fnv_cpu_return_u32(value,8); /* Found or not, LastError is untouched. */
}
enum fnv_cpu_result fnv_windows_user32_create_with_metrics(struct fnv_windows_process *process,const struct fnv_user32_metrics *metrics,struct fnv_windows_user32 **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!process)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_windows_user32 *u=calloc(1,sizeof(*u));if(!u)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&u->guard)){free(u);return FNV_CPU_NO_MEMORY;}
    u->cpu=fnv_windows_runtime(process);u->next=1;if(metrics)u->metrics=*metrics;
    fnv_cpu_function function=api_find_window_a;
    enum fnv_cpu_result r=fnv_cpu_register_native(u->cpu,&function,1,&u->token);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&u->guard);free(u);return r;}
    active=u;*out=u;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_user32_create(struct fnv_windows_process *process,struct fnv_windows_user32 **out){
    return fnv_windows_user32_create_with_metrics(process,NULL,out);
}
enum fnv_cpu_result fnv_windows_user32_destroy(struct fnv_windows_user32 *u){
    if(!u || active!=u)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(u->cpu,u->token,1);if(r!=FNV_CPU_OK)return r;
    active=NULL;fnv_host_mutex_destroy(&u->guard);free(u);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_user32_add_window(struct fnv_windows_user32 *u,const char *class_name,const char *title,uint32_t *handle){
    if(!handle)return FNV_CPU_INVALID;
    *handle=0;if(!u || u!=active)return FNV_CPU_INVALID;
    struct guest_window w={0};
    if(!ascii_copy(w.class_name,sizeof(w.class_name),class_name) || !w.class_name[0] ||
       !ascii_copy(w.title,sizeof(w.title),title))return FNV_CPU_INVALID;
    enum fnv_cpu_result r=FNV_CPU_NO_MEMORY;fnv_host_mutex_lock(&u->guard);
    if(u->next<0x10000000u)for(unsigned i=0;i<16;i++)if(!u->windows[i].handle){
        w.handle=0xb0000000u|u->next++;u->windows[i]=w;*handle=w.handle;r=FNV_CPU_OK;break;}
    fnv_host_mutex_unlock(&u->guard);return r;
}
enum fnv_cpu_result fnv_windows_user32_remove_window(struct fnv_windows_user32 *u,uint32_t handle){
    if(!u || u!=active || !handle)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=FNV_CPU_INVALID;fnv_host_mutex_lock(&u->guard);
    for(unsigned i=0;i<16;i++)if(u->windows[i].handle==handle){memset(u->windows+i,0,sizeof(u->windows[i]));r=FNV_CPU_OK;break;}
    fnv_host_mutex_unlock(&u->guard);return r;
}
static void api_system_metrics(void){
    uint32_t index=fnv_cpu_argument_u32(0);
    (void)fnv_windows_current_address_space_bytes(); /* Require attached guest thread. */
    /* SM_REMOTESESSION: the Switch guest has a local console, no Terminal
     * Services client. Do not infer unrelated metrics from this boolean. */
    if(index==23){fnv_cpu_return_u32(fnv_windows_mouse_buttons(-1),4);return;}
    if(index!=0x1000u){
        if(active && active->cpu==fnv_cpu_current() && active->metrics.query){
            int32_t value=0;int found=active->metrics.query(active->metrics.context,index,&value);
            if(found==1){fnv_cpu_return_u32((uint32_t)value,4);return;}
            if(found){recomp_unimpl("Windows native display metric failed",index);return;}
        }
        recomp_unimpl("Windows system metric pending",index);return;
    }
    fnv_cpu_return_u32(0,4); /* Preserve LastError, including when the metric is zero. */
}
static const struct fnv_cpu_export exports[]={
    {"user32.dll","GetSystemMetrics",0,0,api_system_metrics},
    {"user32.dll","FindWindowA",0,0,api_find_window_a}
};
const struct fnv_cpu_export *fnv_windows_user32_exports(size_t *count){
    if(count)*count=sizeof(exports)/sizeof(*exports);
    return exports;
}
