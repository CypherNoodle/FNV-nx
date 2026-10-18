#include "fnv_sdk_runtime.h"
#include "xinput.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct pad {unsigned char data[12];uint32_t packet;int connected;};
struct fnv_xinput {struct fnv_cpu_runtime *cpu;struct fnv_xinput_config config;fnv_host_mutex guard;struct pad pads[4];uint32_t first_token;};
static struct fnv_xinput *active_input;
static void put16(unsigned char *p,uint16_t v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static struct fnv_xinput *current(void){
    if(!active_input || fnv_cpu_current()!=active_input->cpu)recomp_unimpl("XInput provider unavailable",0);
    return active_input;
}
static unsigned char *range(uint32_t at,uint32_t size,int write_access){
    if(!at || (uint64_t)at+size>UINT64_C(0x100000000))recomp_unimpl("XInput invalid buffer",at);
    unsigned char *p=fnv_cpu_try_memory(at,size);struct fnv_guest_memory *vm=fnv_windows_current_memory();
    unsigned char *backing;uint32_t base,bytes;
    if(!p || (vm && fnv_vm_binding(vm,&backing,&base,&bytes) && (uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+size>base && !fnv_vm_access(vm,at,size,write_access)))
        recomp_unimpl("XInput inaccessible buffer",at);
    return p;
}
static void api_get(void){
    struct fnv_xinput *x=current();uint32_t user=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),error=0;
    unsigned char bytes[16];
    if(user>=4)error=160;
    else {fnv_host_mutex_lock(&x->guard);struct pad pad=x->pads[user];fnv_host_mutex_unlock(&x->guard);
        if(!pad.connected)error=1167;
        else {put32(bytes,pad.packet);memcpy(bytes+4,pad.data,12);memcpy(range(at,16,1),bytes,16);}}
    fnv_cpu_return_u32(error,8);
}
static void api_set(void){
    struct fnv_xinput *x=current();uint32_t user=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),error=0;
    if(user>=4)error=160;
    else {fnv_host_mutex_lock(&x->guard);int connected=x->pads[user].connected;fnv_host_mutex_unlock(&x->guard);
        if(!connected)error=1167;
        else {const unsigned char *p=range(at,4,0);uint16_t left=p[0]|((uint16_t)p[1]<<8),right=p[2]|((uint16_t)p[3]<<8);
            error=x->config.rumble?x->config.rumble(x->config.context,user,left,right):50;}}
    fnv_cpu_return_u32(error,8);
}
static const struct fnv_cpu_export exports[]={
    {"xinput1_3.dll","XInputGetState",0,0,api_get},{"xinput1_3.dll","XInputSetState",0,0,api_set},
    {"xinput1_3.dll",NULL,2,1,api_get},{"xinput1_3.dll",NULL,3,1,api_set}
};
const struct fnv_cpu_export *fnv_xinput_exports(size_t *n){if(n)*n=4;return exports;}
enum fnv_cpu_result fnv_xinput_create(struct fnv_windows_process *p,const struct fnv_xinput_config *c,struct fnv_xinput **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!p || !c)return FNV_CPU_INVALID;if(active_input)return FNV_CPU_BUSY;
    struct fnv_xinput *x=calloc(1,sizeof(*x));if(!x)return FNV_CPU_NO_MEMORY;
    x->cpu=fnv_windows_runtime(p);x->config=*c;
    if(!fnv_host_mutex_init(&x->guard)){free(x);return FNV_CPU_NO_MEMORY;}
    fnv_cpu_function functions[2]={api_get,api_set};uint32_t tokens[2];
    enum fnv_cpu_result r=fnv_cpu_register_native(x->cpu,functions,2,tokens);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&x->guard);free(x);return r;}
    x->first_token=tokens[0];active_input=x;*out=x;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_xinput_destroy(struct fnv_xinput *x){
    if(!x || x!=active_input)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(x->cpu,x->first_token,2);if(r!=FNV_CPU_OK)return r;
    active_input=NULL;fnv_host_mutex_destroy(&x->guard);free(x);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_xinput_update(struct fnv_xinput *x,unsigned user,int connected,const struct fnv_gamepad *g){
    if(!x || x!=active_input || user>=4 || (connected && !g))return FNV_CPU_INVALID;
    unsigned char data[12]={0};
    if(connected){put16(data,g->buttons);data[2]=g->left_trigger;data[3]=g->right_trigger;
        put16(data+4,(uint16_t)g->lx);put16(data+6,(uint16_t)g->ly);put16(data+8,(uint16_t)g->rx);put16(data+10,(uint16_t)g->ry);}
    fnv_host_mutex_lock(&x->guard);struct pad *p=x->pads+user;
    if(p->connected!=!!connected || memcmp(p->data,data,12))p->packet++;
    p->connected=!!connected;memcpy(p->data,data,12);fnv_host_mutex_unlock(&x->guard);return FNV_CPU_OK;
}
