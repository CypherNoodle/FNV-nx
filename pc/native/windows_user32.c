/* Original local-console USER32 bridge, independent of the host desktop. */
#include "fnv_sdk_runtime.h"
#include "windows_user32.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct guest_window {uint32_t handle;char class_name[256],title[512];};
struct posted_thread_message {uint32_t owner;struct fnv_user32_thread_message value;};
struct fnv_windows_user32 {
    struct fnv_cpu_runtime *cpu;fnv_host_mutex guard;uint32_t tokens[3],next;
    struct guest_window windows[16];struct fnv_user32_metrics metrics;
    struct posted_thread_message posted[256];uint32_t posted_count;
    unsigned char keys[256],pressed[256];int input_ready,input_focused;
};
/* One virtual desktop, isolated from the PC host. Lifecycle matches the native
 * module catalog: create before attach, destroy after every thread detaches. */
static struct fnv_windows_user32 *active;
static void api_peek_message_a(void);
static void api_async_key(void);
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
    fnv_cpu_function functions[]={api_find_window_a,api_peek_message_a,api_async_key};
    enum fnv_cpu_result r=fnv_cpu_register_native(u->cpu,functions,3,u->tokens);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&u->guard);free(u);return r;}
    active=u;*out=u;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_user32_create(struct fnv_windows_process *process,struct fnv_windows_user32 **out){
    return fnv_windows_user32_create_with_metrics(process,NULL,out);
}
enum fnv_cpu_result fnv_windows_user32_destroy(struct fnv_windows_user32 *u){
    if(!u || active!=u)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(u->cpu,u->tokens[0],3);if(r!=FNV_CPU_OK)return r;
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
enum fnv_cpu_result fnv_windows_user32_post_thread(struct fnv_windows_user32 *u,uint32_t id,const struct fnv_user32_thread_message *m){
    if(!u || u!=active || !id || !m)return FNV_CPU_INVALID;
    fnv_host_mutex_lock(&u->guard);enum fnv_cpu_result result=FNV_CPU_NO_MEMORY;
    if(u->posted_count<256){u->posted[u->posted_count++]=(struct posted_thread_message){id,*m};result=FNV_CPU_OK;}
    fnv_host_mutex_unlock(&u->guard);return result;
}
static void message_write32(unsigned char *p,uint32_t value){for(unsigned j=0;j<4;j++)p[j]=(unsigned char)(value>>(j*8));}
static void api_peek_message_a(void){
    uint32_t at=fnv_cpu_argument_u32(0),hwnd=fnv_cpu_argument_u32(1),low=fnv_cpu_argument_u32(2),high=fnv_cpu_argument_u32(3),flags=fnv_cpu_argument_u32(4);
    if(!active || active->cpu!=fnv_cpu_current()){recomp_unimpl("Windows message queue unavailable",at);return;}
    if((hwnd && hwnd!=UINT32_MAX) || (flags&~3u) || ((low|high)&0xffff0000u)){
        recomp_unimpl("Windows PeekMessage window/queue-status filter pending",hwnd);return;}
    unsigned char *output=at?fnv_cpu_try_memory(at,28):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&backing,&base,&bytes) ||
           ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+28>base && !fnv_vm_access(vm,at,28,1)))output=NULL;}
    if(!output){recomp_unimpl("Windows PeekMessage output unavailable",at);return;}
    uint32_t stack_low,stack_high,id,value=0;fnv_windows_current_stack_bounds(&stack_low,&stack_high,&id);
    struct fnv_user32_thread_message found={0};
    fnv_host_mutex_lock(&active->guard);
    for(uint32_t i=0;i<active->posted_count;i++){
        struct posted_thread_message *p=active->posted+i;
        if(p->owner!=id || (p->value.message!=0x12 && (low || high) && (p->value.message<low || p->value.message>high)))continue;
        found=p->value;value=1;
        if(flags&1){memmove(p,p+1,(active->posted_count-i-1)*sizeof(*p));active->posted_count--;}
        break;
    }
    fnv_host_mutex_unlock(&active->guard);
    if(value){uint32_t words[]={0,found.message,found.wparam,found.lparam,found.time,(uint32_t)found.x,(uint32_t)found.y};
        for(unsigned i=0;i<7;i++)message_write32(output+i*4,words[i]);}
    fnv_cpu_return_u32(value,20);
}
enum fnv_cpu_result fnv_windows_user32_update_keys(struct fnv_windows_user32 *u,const unsigned char keys[256],int focused){
    if(!u || u!=active || !keys || (focused!=0 && focused!=1))return FNV_CPU_INVALID;
    unsigned char next[256];
    for(unsigned i=0;i<256;i++){if(keys[i]!=0 && keys[i]!=128)return FNV_CPU_INVALID;next[i]=keys[i];}
    next[0x10]=next[0xa0]|next[0xa1];next[0x11]=next[0xa2]|next[0xa3];next[0x12]=next[0xa4]|next[0xa5];
    fnv_host_mutex_lock(&u->guard);
    for(unsigned i=0;i<256;i++){
        if(!focused){u->keys[i]=u->pressed[i]=0;continue;}
        if(next[i] && !u->keys[i])u->pressed[i]=1;
        u->keys[i]=next[i];
    }
    u->input_ready=1;u->input_focused=focused;fnv_host_mutex_unlock(&u->guard);return FNV_CPU_OK;
}
static void api_async_key(void){
    uint32_t key=fnv_cpu_argument_u32(0),value=0;
    if(!active || active->cpu!=fnv_cpu_current()){recomp_unimpl("Windows async key provider unavailable",key);return;}
    if(key>255){fnv_cpu_return_u32(0,4);return;}
    fnv_host_mutex_lock(&active->guard);int ready=active->input_ready;
    if(ready && active->input_focused){value=(active->keys[key]?0x8000u:0)|active->pressed[key];active->pressed[key]=0;}
    fnv_host_mutex_unlock(&active->guard);
    if(!ready){recomp_unimpl("Windows async key snapshot unavailable",key);return;}
    fnv_cpu_return_u32(value,4);
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
    {"user32.dll","FindWindowA",0,0,api_find_window_a},
    {"user32.dll","PeekMessageA",0,0,api_peek_message_a},
    {"user32.dll","GetAsyncKeyState",0,0,api_async_key}
};
const struct fnv_cpu_export *fnv_windows_user32_exports(size_t *count){
    if(count)*count=sizeof(exports)/sizeof(*exports);
    return exports;
}
