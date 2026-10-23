/* Original Bink sound selection bridge. Native behavior checked separately. */
#include "fnv_sdk_runtime.h"
#include "bink_sound.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct fnv_bink_sound {
    struct fnv_cpu_runtime *cpu;struct fnv_bink_sound_config config;
    struct fnv_bink_sound_state selected;fnv_host_mutex guard;
    uint32_t tokens[2];int changing;
};
static struct fnv_bink_sound *active;
static void wr(unsigned char *p,uint32_t value){memcpy(p,&value,4);}
static unsigned char *range(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,size;
        if(!fnv_vm_binding(vm,&backing,&base,&size) ||
           ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))p=NULL;}
    if(!p)recomp_unimpl("Bink sound callback memory unavailable",at);
    return p;
}
static int current(void){
    if(active && active->cpu==fnv_cpu_current())return 1;
    recomp_unimpl("Bink sound provider unavailable",0);return 0;
}
static void api_set_system(void){
    uint32_t factory=fnv_cpu_argument_u32(0),parameter=fnv_cpu_argument_u32(1);if(!current())return;
    fnv_host_mutex_lock(&active->guard);
    if(active->changing){fnv_host_mutex_unlock(&active->guard);recomp_unimpl("Bink concurrent or recursive sound selection pending",factory);return;}
    if(!factory){memset(&active->selected,0,sizeof(active->selected));fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(0,8);return;}
    active->changing=1;fnv_host_mutex_unlock(&active->guard);
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || (state.gpr[4]&3) || state.gpr[4]>high || state.gpr[4]<low || state.gpr[4]-low<160){
        recomp_unimpl("Bink sound callback stack unavailable",factory);return;}
    uint32_t frame=state.gpr[4]-160;unsigned char *p=range(frame,8,1),*ret=range(state.gpr[4],4,0);if(!p || !ret)return;
    memcpy(p,ret,4);wr(p+4,parameter);
    /* Nested execution preserves CPU/x87/vector state; callback memory effects
     * and LastError remain visible. No host lock is held across guest code. */
    uint32_t open=fnv_cpu_invoke_stdcall(factory,frame,4);
    fnv_host_mutex_lock(&active->guard);
    if(open)active->selected=(struct fnv_bink_sound_state){factory,parameter,open};
    active->changing=0;uint32_t accepted=active->selected.open!=0;fnv_host_mutex_unlock(&active->guard);
    fnv_cpu_return_u32(accepted,8);
}
static void api_open_directsound(void){
    uint32_t device=fnv_cpu_argument_u32(0);if(!current())return;
    if(!active->config.open_directsound){recomp_unimpl("Bink DirectSound transport unavailable",device);return;}
    uint32_t entry=active->config.open_directsound(active->config.context,device);
    fnv_cpu_return_u32(entry,4);
}
static const struct fnv_cpu_export exports[]={
    {"binkw32.dll","_BinkSetSoundSystem@8",0,0,api_set_system},
    {"binkw32.dll","_BinkOpenDirectSound@4",0,0,api_open_directsound}};
const struct fnv_cpu_export *fnv_bink_sound_exports(size_t *n){if(n)*n=2;return exports;}
enum fnv_cpu_result fnv_bink_sound_create(struct fnv_windows_process *process,const struct fnv_bink_sound_config *cfg,struct fnv_bink_sound **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);if(!cpu || !cfg)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_bink_sound *s=calloc(1,sizeof(*s));if(!s)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&s->guard)){free(s);return FNV_CPU_NO_MEMORY;}
    s->cpu=cpu;s->config=*cfg;fnv_cpu_function functions[]={api_set_system,api_open_directsound};
    enum fnv_cpu_result r=fnv_cpu_register_native(cpu,functions,2,s->tokens);
    if(r){fnv_host_mutex_destroy(&s->guard);free(s);return r;}
    active=s;*out=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_bink_sound_destroy(struct fnv_bink_sound *s){
    if(!s || s!=active)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(s->cpu,s->tokens[0],2);if(r)return r;
    active=NULL;fnv_host_mutex_destroy(&s->guard);free(s);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_bink_sound_state(struct fnv_bink_sound *s,struct fnv_bink_sound_state *out){
    if(!s || s!=active || !out)return FNV_CPU_INVALID;
    fnv_host_mutex_lock(&s->guard);*out=s->selected;fnv_host_mutex_unlock(&s->guard);return FNV_CPU_OK;
}
