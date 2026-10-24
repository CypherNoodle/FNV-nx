/* Original bounded registry for the public Steam CCallbackBase x86 ABI. */
#include "fnv_sdk_runtime.h"
#include "steam_callbacks.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
struct callback_entry {uint32_t object,id;};
struct fnv_steam_callbacks {
    struct fnv_cpu_runtime *cpu;fnv_host_mutex guard;uint32_t tokens[5];
    struct callback_entry entries[128];
};
static struct fnv_steam_callbacks *active;
static unsigned char *object_memory(uint32_t at){
    if(!active || active->cpu!=fnv_cpu_current() || !at)return NULL;
    unsigned char *p=fnv_cpu_try_memory(at,12);
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&backing,&base,&bytes) ||
           ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+12>base && !fnv_vm_access(vm,at,12,1)))return NULL;}
    return p;
}
static void registration(int remove){
    uint32_t at=fnv_cpu_argument_u32(0),id=remove?0:fnv_cpu_argument_u32(1);
    unsigned char *p=object_memory(at);
    if(!p){recomp_unimpl("Steam callback object unavailable",at);return;}
    int index=-1,empty=-1,bad=0;
    fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<128;i++){
        if(active->entries[i].object==at)index=(int)i;
        if(!active->entries[i].object && empty<0)empty=(int)i;
    }
    /* Unknown flags or a registered bit not owned by this registry are not
     * silently accepted. Rebinding an object to another ID is also pending. */
    if((p[4]&~3u) || ((p[4]&1u)!=0)!=(index>=0))bad=1;
    else if(remove){
        if(index>=0){active->entries[index]=(struct callback_entry){0,0};p[4]&=(unsigned char)~1u;}
    }else if(index>=0){
        if(active->entries[index].id!=id)bad=1;
    }else if(empty<0)bad=1;
    else{
        active->entries[empty]=(struct callback_entry){at,id};
        for(unsigned i=0;i<4;i++)p[8+i]=(unsigned char)(id>>(8*i));
        p[4]|=1;
    }
    fnv_host_mutex_unlock(&active->guard);
    if(bad){recomp_unimpl("Steam callback registry variant/capacity pending",at);return;}
    fnv_cpu_return_u32(g_eax,0); /* void cdecl: caller owns all arguments. */
}
static void api_register(void){registration(0);}
static void api_unregister(void){registration(1);}
static void api_init(void){
    if(!active || active->cpu!=fnv_cpu_current()){
        recomp_unimpl("Steam initialization provider unavailable",0);return;}
    /* No Steam client exists in this guest environment. Valve documents false
     * for this case. No interfaces, session or callback events are created. */
    fnv_cpu_return_u32(0,0);
}
static void api_running(void){
    if(!active || active->cpu!=fnv_cpu_current()){
        recomp_unimpl("Steam client-state provider unavailable",0);return;}
    /* Guest environment has no Steam client. Never report authentication or
     * a live client based on the host PC's unrelated Steam installation. */
    fnv_cpu_return_u32(0,0);
}
static void api_run_callbacks(void){
    if(!active || active->cpu!=fnv_cpu_current()){
        recomp_unimpl("Steam callback pump provider unavailable",0);return;}
    /* Init never succeeds and no client/event source exists. Therefore there
     * are no events to deliver. Registrations remain intact for unregister. */
    fnv_cpu_return_u32(g_eax,0);
}
static const struct fnv_cpu_export exports[]={
    {"steam_api.dll","SteamAPI_RegisterCallback",0,0,api_register},
    {"steam_api.dll","SteamAPI_UnregisterCallback",0,0,api_unregister},
    {"steam_api.dll","SteamAPI_Init",0,0,api_init},
    {"steam_api.dll","SteamAPI_IsSteamRunning",0,0,api_running},
    {"steam_api.dll","SteamAPI_RunCallbacks",0,0,api_run_callbacks}
};
const struct fnv_cpu_export *fnv_steam_callbacks_exports(size_t *count){if(count)*count=5;return exports;}
enum fnv_cpu_result fnv_steam_callbacks_create(struct fnv_windows_process *process,struct fnv_steam_callbacks **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!process)return FNV_CPU_INVALID;if(active)return FNV_CPU_BUSY;
    struct fnv_steam_callbacks *s=calloc(1,sizeof(*s));if(!s)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&s->guard)){free(s);return FNV_CPU_NO_MEMORY;}
    s->cpu=fnv_windows_runtime(process);fnv_cpu_function functions[]={api_register,api_unregister,api_init,api_running,api_run_callbacks};
    enum fnv_cpu_result r=fnv_cpu_register_native(s->cpu,functions,5,s->tokens);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&s->guard);free(s);return r;}
    active=s;*out=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_steam_callbacks_destroy(struct fnv_steam_callbacks *s){
    if(!s || active!=s)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(s->cpu,s->tokens[0],5);if(r!=FNV_CPU_OK)return r;
    /* Process teardown discards host bookkeeping without touching guest objects. */
    active=NULL;fnv_host_mutex_destroy(&s->guard);free(s);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_steam_callbacks_count(struct fnv_steam_callbacks *s,uint32_t *out){
    if(!s || active!=s || !out)return FNV_CPU_INVALID;
    uint32_t n=0;fnv_host_mutex_lock(&s->guard);
    for(unsigned i=0;i<128;i++)if(s->entries[i].object)n++;
    fnv_host_mutex_unlock(&s->guard);*out=n;return FNV_CPU_OK;
}
