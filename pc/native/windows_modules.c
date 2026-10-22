/* Original native module namespace. No Windows loader code is incorporated. */
#include "fnv_sdk_runtime.h"
#include "windows_modules.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
#ifdef FNV_MODULE_TRACE
#include <stdio.h>
#endif
struct module_item {char name[128];uint32_t handle,references;};
struct module_export {char name[256];uint16_t ordinal;unsigned by_ordinal,module;uint32_t token;fnv_cpu_function function;};
struct fnv_windows_modules {
    struct fnv_cpu_runtime *cpu;fnv_host_mutex guard;char exe[128];uint32_t image_base;
    size_t modules,count,method_count;struct module_item *items;struct module_export *symbols;
    uint32_t first_token;
    uint64_t load_calls;
};
static struct fnv_windows_modules *active_modules;
static int ascii_copy(const char *in,char *out,size_t capacity){
    if(!in || !*in)return 0;
    for(size_t i=0;i<capacity;++i){unsigned c=(unsigned char)in[i];if(c>127)return 0;out[i]=(char)c;if(!c)return 1;}
    return 0;
}
static int canonical(char *s){
    size_t n=strlen(s);if(!n || n>123)return 0;int dot=0;
    for(size_t i=0;i<n;++i){unsigned c=(unsigned char)s[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='.' || c=='_' || c=='-'))return 0;
        if(c>='A'&&c<='Z')s[i]=(char)(c+32);
        dot|=c=='.';}
    if(s[n-1]=='.')s[n-1]=0;else if(!dot)memcpy(s+n,".dll",5);
    return *s!=0;
}
static struct fnv_windows_modules *modules(void){
    struct fnv_windows_modules *m=active_modules;
    if(!m || m->cpu!=fnv_cpu_current())recomp_unimpl("Windows native module provider unavailable",0);
    return m;
}
static int guest_string(uint32_t at,int wide,char *out,size_t capacity){
    if(!at)recomp_unimpl("Windows module string is NULL",at);
    struct fnv_guest_memory *vm=fnv_windows_current_memory();unsigned char *backing=NULL;uint32_t base=0,bytes=0;
    if(vm && !fnv_vm_binding(vm,&backing,&base,&bytes))recomp_unimpl("Windows module VM unavailable",at);
    for(size_t i=0;i<capacity;++i){uint64_t pos=(uint64_t)at+i*(wide?2u:1u);unsigned width=wide?2:1;
        if(pos+width>UINT64_C(0x100000000))recomp_unimpl("Windows module string wraps",at);
        unsigned char *p=fnv_cpu_try_memory((uint32_t)pos,width);
        if(!p || (vm && pos<(uint64_t)base+bytes && pos+width>base && !fnv_vm_access(vm,(uint32_t)pos,width,0)))
            recomp_unimpl("Windows module string unreadable",(uint32_t)pos);
        unsigned c=p[0]|(wide?((unsigned)p[1]<<8):0);if(c>127)return 0;out[i]=(char)c;if(!c)return i!=0;
    }
    return 0;
}
static void module_get(int wide,int load){
    uint32_t at=fnv_cpu_argument_u32(0),value=0,error=126;char name[128]={0};struct fnv_windows_modules *m=modules();
    if(!at && !load)value=m->image_base;
    else if(guest_string(at,wide,name,sizeof(name)) && canonical(name)){
        if(!strcmp(name,m->exe) && !load)value=m->image_base;
        else {fnv_host_mutex_lock(&m->guard);
            for(size_t i=0;i<m->modules;++i)if(!strcmp(name,m->items[i].name)){
                if(load && m->items[i].references==UINT32_MAX)error=8;
                else {if(load){m->items[i].references++;m->load_calls++;}value=m->items[i].handle;}break;
            }
            fnv_host_mutex_unlock(&m->guard);}
    }
#ifdef FNV_MODULE_TRACE
    fprintf(stderr,"FNV_MODULE %s wide=%d name=%.127s result=%08x error=%u\n",load?"load":"get",wide,name,value,value?0:error);
#endif
    if(!value)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,4);
}
static void api_load_a(void){module_get(0,1);}static void api_load_w(void){module_get(1,1);}
static void api_get_a(void){module_get(0,0);}static void api_get_w(void){module_get(1,0);}
static void api_free(void){
    uint32_t h=fnv_cpu_argument_u32(0),value=0;struct fnv_windows_modules *m=modules();
    fnv_host_mutex_lock(&m->guard);
    for(size_t i=0;i<m->modules;++i)if(h==m->items[i].handle){
        /* Native implementations are pinned for process lifetime. */
        if(m->items[i].references)m->items[i].references--;
        value=1;break;}
    fnv_host_mutex_unlock(&m->guard);if(!value)fnv_windows_set_last_error(6);fnv_cpu_return_u32(value,4);
}
static void api_proc(void){
    uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),value=0;char name[256]={0};
    struct fnv_windows_modules *m=modules();int ordinal=at<=65535;
    if(ordinal || guest_string(at,0,name,sizeof(name))){
        for(size_t i=0;i<m->count;++i){struct module_export *s=m->symbols+i;
            if(h==m->items[s->module].handle && s->by_ordinal==(unsigned)ordinal &&
               (ordinal?s->ordinal==at:!strcmp(name,s->name))){value=fnv_cpu_import_token(s->function);if(!value)value=s->token;break;}}
    }
#ifdef FNV_MODULE_TRACE
    fprintf(stderr,"FNV_MODULE proc module=%08x ordinal=%u name=%.255s result=%08x\n",h,ordinal?at:0,ordinal?"":name,value);
#endif
    if(!value)fnv_windows_set_last_error(127);
    fnv_cpu_return_u32(value,8);
}
static const struct fnv_cpu_export module_api[FNV_WINDOWS_MODULE_EXPORTS]={
    {"kernel32.dll","LoadLibraryA",0,0,api_load_a},{"kernel32.dll","LoadLibraryW",0,0,api_load_w},
    {"kernel32.dll","GetModuleHandleA",0,0,api_get_a},{"kernel32.dll","GetModuleHandleW",0,0,api_get_w},
    {"kernel32.dll","GetProcAddress",0,0,api_proc},{"kernel32.dll","FreeLibrary",0,0,api_free}
};
const struct fnv_cpu_export *fnv_windows_modules_exports(size_t *count){if(count)*count=FNV_WINDOWS_MODULE_EXPORTS;return module_api;}
enum fnv_cpu_result fnv_windows_modules_stats(struct fnv_windows_modules *m,struct fnv_windows_module_stats *out){
    if(!m || !out || active_modules!=m)return FNV_CPU_INVALID;
    struct fnv_windows_module_stats s={0};fnv_host_mutex_lock(&m->guard);
    s.pinned_modules=m->modules;s.native_exports=m->count;s.load_calls=m->load_calls;
    for(size_t i=0;i<m->modules;++i)s.dynamic_references+=m->items[i].references;
    fnv_host_mutex_unlock(&m->guard);*out=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_modules_create(struct fnv_windows_process *p,const struct fnv_native_image *image,const char *exe,
    const struct fnv_cpu_export *catalog,size_t count,struct fnv_windows_modules **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!p || !image || !image->data || !image->size || !image->base || !catalog || !count || count>FNV_CPU_MAX_NATIVE_METHODS)return FNV_CPU_INVALID;
    if(active_modules)return FNV_CPU_BUSY;
    struct fnv_windows_modules *m=calloc(1,sizeof(*m));if(!m)return FNV_CPU_NO_MEMORY;
    enum fnv_cpu_result result=FNV_CPU_INVALID;
    void *image_data=NULL;m->cpu=fnv_windows_runtime(p);
    result=fnv_cpu_prepare_memory(m->cpu,image->base,image->size,&image_data);
    if(result!=FNV_CPU_OK)goto failed;
    result=FNV_CPU_INVALID;if(image_data!=image->data)goto failed;m->image_base=image->base;
    if(!ascii_copy(exe,m->exe,sizeof(m->exe)) || !canonical(m->exe))goto failed;
    m->items=calloc(count,sizeof(*m->items));m->symbols=calloc(count,sizeof(*m->symbols));
    if(!m->items || !m->symbols){result=FNV_CPU_NO_MEMORY;goto failed;}
    fnv_cpu_function functions[FNV_CPU_MAX_NATIVE_METHODS];uint32_t tokens[FNV_CPU_MAX_NATIVE_METHODS];
    for(size_t i=0;i<count;++i){const struct fnv_cpu_export *e=catalog+i;char dll[128];
        if(!e->function || (e->by_ordinal!=0 && e->by_ordinal!=1) || !ascii_copy(e->dll,dll,sizeof(dll)) || !canonical(dll) || !strcmp(dll,m->exe))goto failed;
        struct module_export *s=m->symbols+i;s->by_ordinal=e->by_ordinal;s->ordinal=e->ordinal;s->function=e->function;
        if((s->by_ordinal && !s->ordinal) || (!s->by_ordinal && !ascii_copy(e->name,s->name,sizeof(s->name))))goto failed;
        size_t index=0;while(index<m->modules && strcmp(dll,m->items[index].name))index++;
        if(index==m->modules){memcpy(m->items[index].name,dll,strlen(dll)+1);m->items[index].handle=0xF0000000u+(uint32_t)(index+1)*65536;
            if(m->items[index].handle==m->image_base)goto failed;
            m->modules++;}
        s->module=(unsigned)index;
        for(size_t j=0;j<i;++j){struct module_export *other=m->symbols+j;
            if(other->module==s->module && other->by_ordinal==s->by_ordinal && (s->by_ordinal?other->ordinal==s->ordinal:!strcmp(other->name,s->name)))goto failed;}
        size_t method=0;while(method<m->method_count && functions[method]!=e->function)method++;
        if(method==m->method_count)functions[m->method_count++]=e->function;
        s->token=(uint32_t)method;
    }
    if(!fnv_host_mutex_init(&m->guard)){result=FNV_CPU_NO_MEMORY;goto failed;}
    result=fnv_cpu_register_native(m->cpu,functions,m->method_count,tokens);
    if(result!=FNV_CPU_OK){fnv_host_mutex_destroy(&m->guard);goto failed;}
    for(size_t i=0;i<count;++i)m->symbols[i].token=tokens[m->symbols[i].token];
    m->first_token=tokens[0];m->count=count;active_modules=m;*out=m;return FNV_CPU_OK;
failed:free(m->symbols);free(m->items);free(m);return result;
}
enum fnv_cpu_result fnv_windows_modules_destroy(struct fnv_windows_modules *m){
    if(!m || active_modules!=m)return FNV_CPU_INVALID;
    enum fnv_cpu_result result=fnv_cpu_unregister_native(m->cpu,m->first_token,m->method_count);if(result!=FNV_CPU_OK)return result;
    active_modules=NULL;fnv_host_mutex_destroy(&m->guard);free(m->symbols);free(m->items);free(m);return FNV_CPU_OK;
}
