/* Original x86 MMIO ABI over translated custom I/O callbacks. */
#include "fnv_sdk_runtime.h"
#include "windows_mmio.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
#define SLOT_COUNT 8u
#define SLOT_STRIDE 0x2100u
#define BUFFER_BYTES 8192u
struct mmio_slot {uint32_t generation,phase,info,buffer,procedure,handle;};
struct fnv_windows_mmio {struct fnv_cpu_runtime *cpu;struct fnv_mmio_config config;fnv_host_mutex guard;struct mmio_slot slots[SLOT_COUNT];uint32_t tokens[9];};
static struct fnv_windows_mmio *active;
static uint32_t get32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t v){for(unsigned j=0;j<4;j++)p[j]=(unsigned char)(v>>(8*j));}
static void bad(const char *text,uint32_t at){recomp_unimpl(text,at);}
static unsigned char *checked(uint32_t at,uint32_t n,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,n):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&backing,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))p=NULL;}
    if(!p)bad("MMIO guest memory unavailable",at);return p;
}
static int ready(void){if(active && active->cpu==fnv_cpu_current())return 1;bad("MMIO provider unavailable",0);return 0;}
static uint32_t callback(struct mmio_slot *s,uint32_t msg,uint32_t a,uint32_t b){
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || state.gpr[4]<low || state.gpr[4]>high || state.gpr[4]-low<160 || (state.gpr[4]&3)){
        bad("MMIO callback stack unavailable",s->procedure);return 0;}
    uint32_t frame=state.gpr[4]-160;unsigned char *p=checked(frame,20,1),*ret=checked(state.gpr[4],4,0);if(!p || !ret)return 0;
    put32(p,get32(ret));put32(p+4,s->info);put32(p+8,msg);put32(p+12,a);put32(p+16,b);
    return fnv_cpu_invoke_stdcall(s->procedure,frame,16);
}
static struct mmio_slot *acquire(uint32_t handle){
    if(!ready())return NULL;
    struct mmio_slot *s=NULL;int busy=0;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<SLOT_COUNT;i++)if(active->slots[i].handle==handle && active->slots[i].phase){
        if(active->slots[i].phase==2){s=active->slots+i;s->phase=3;}else busy=1;break;}
    fnv_host_mutex_unlock(&active->guard);if(busy)bad("MMIO concurrent or recursive stream access pending",handle);return s;
}
static void release(struct mmio_slot *s){fnv_host_mutex_lock(&active->guard);s->phase=2;fnv_host_mutex_unlock(&active->guard);}
static unsigned char *info(struct mmio_slot *s){return checked(s->info,72,1);}
static uint32_t position(struct mmio_slot *s){
    unsigned char *p=info(s);if(!p)return UINT32_MAX;
    uint32_t next=get32(p+28),end=get32(p+32),off=get32(p+40);
    if(get32(p+24)!=s->buffer || next<s->buffer || next>end || end>s->buffer+BUFFER_BYTES || off>INT32_MAX || (uint64_t)off+next-s->buffer>INT32_MAX){bad("MMIO buffer state invalid",s->info);return UINT32_MAX;}
    return off+next-s->buffer;
}
static void empty_buffer(struct mmio_slot *s,uint32_t pos){unsigned char *p=info(s);if(!p)return;put32(p+28,s->buffer);put32(p+32,s->buffer);put32(p+40,pos);}
static int fill(struct mmio_slot *s){
    uint32_t pos=position(s);if(pos==UINT32_MAX)return -1;unsigned char *p=info(s);if(!p)return -1;
    if(get32(p+44)!=pos && callback(s,2,pos,0)!=pos)return -1;
    empty_buffer(s,pos);int32_t n=(int32_t)callback(s,0,s->buffer,BUFFER_BYTES);
    if(n<0)return -1;if(n>(int32_t)BUFFER_BYTES){bad("MMIO I/O procedure read count invalid",s->procedure);return -1;}
    put32(p+32,s->buffer+(uint32_t)n);return n;
}
static int32_t stream_read(struct mmio_slot *s,uint32_t at,uint32_t bytes){
    unsigned char *output=bytes?checked(at,bytes,1):NULL;if(bytes && !output)return -1;uint32_t done=0;
    while(done<bytes){unsigned char *p=info(s);if(!p)return -1;uint32_t available=get32(p+32)-get32(p+28);(void)position(s);
        if(!available){int got=fill(s);if(got<=0)return got<0 && !done?-1:(int32_t)done;continue;}
        uint32_t n=bytes-done;if(n>available)n=available;unsigned char *input=checked(get32(p+28),n,0);if(!input)return -1;
        memmove(output+done,input,n);put32(p+28,get32(p+28)+n);done+=n;
    }return (int32_t)done;
}
static int32_t stream_seek(struct mmio_slot *s,int32_t distance,uint32_t origin){
    uint32_t pos=position(s);if(pos==UINT32_MAX)return -1;if(origin>2)return -1;
    unsigned char *p=info(s);if(!p)return -1;
    if(origin==2){
        if(distance){bad("MMIO nonzero seek-from-end displacement pending",(uint32_t)distance);return -1;}
        int32_t end=(int32_t)callback(s,2,0,2);if(end<0)return -1;
        empty_buffer(s,(uint32_t)end);return end;
    }
    int64_t target=(origin==1?(int64_t)pos:0)+distance;if(target<0 || target>INT32_MAX)return -1;
    uint32_t begin=get32(p+40),end=begin+get32(p+32)-s->buffer;
    if((uint32_t)target>=begin && (uint32_t)target<=end){put32(p+28,s->buffer+(uint32_t)target-begin);return (int32_t)target;}
    int32_t result=(int32_t)callback(s,2,(uint32_t)target,0);if(result<0)return -1;empty_buffer(s,(uint32_t)result);return result;
}
static void api_open(void){
    uint32_t name=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(!ready())return;
    unsigned char *source=at?checked(at,72,1):NULL;
    if(!source || flags!=0x10000 || !get32(source+8) || get32(source+20) || get32(source+24)){
        bad("MMIO open mode or custom procedure pending",at);return;}
    for(unsigned i=0;i<72;i+=4)if(i!=4 && i!=8 && !(i>=48 && i<60) && get32(source+i)){bad("MMIO custom open fields pending",at+i);return;}
    if(name){unsigned i;for(i=0;i<512;i++){unsigned char *c=checked(name+i,1,0);if(!c)return;if(!*c)break;}if(i==512){bad("MMIO file name length pending",name);return;}}
    struct mmio_slot *s=NULL;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<SLOT_COUNT;i++)if(!active->slots[i].phase && active->slots[i].generation<0xfffffu){s=active->slots+i;s->phase=1;s->generation++;s->handle=0x9a000000u|(s->generation<<4)|i;break;}
    fnv_host_mutex_unlock(&active->guard);
    if(!s){put32(source+12,258);fnv_cpu_return_u32(0,12);return;}
    s->procedure=get32(source+8);unsigned char *p=info(s);if(!p)return;memmove(p,source,72);put32(p,flags);put32(p+20,BUFFER_BYTES);put32(p+24,s->buffer);put32(p+28,s->buffer);put32(p+32,s->buffer);put32(p+36,s->buffer+BUFFER_BYTES);put32(p+68,s->handle);
    uint32_t error=callback(s,3,name,flags);
    if(error){put32(source+12,error);fnv_host_mutex_lock(&active->guard);s->phase=0;fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(0,12);return;}
    release(s);fnv_cpu_return_u32(s->handle,12);
}
static void api_close(void){
    uint32_t handle=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1);if(flags){bad("MMIO close flags pending",flags);return;}
    struct mmio_slot *s=acquire(handle);if(!s){fnv_cpu_return_u32(5,8);return;}
    uint32_t result=callback(s,4,0,0);fnv_host_mutex_lock(&active->guard);s->phase=0;fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(result,8);
}
static void api_read(void){uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),n=fnv_cpu_argument_u32(2);if(n>INT32_MAX){bad("MMIO read size invalid",n);return;}struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(UINT32_MAX,12);return;}int32_t result=stream_read(s,at,n);release(s);fnv_cpu_return_u32((uint32_t)result,12);}
static void api_seek(void){uint32_t h=fnv_cpu_argument_u32(0),distance=fnv_cpu_argument_u32(1),origin=fnv_cpu_argument_u32(2);struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(UINT32_MAX,12);return;}int32_t result=stream_seek(s,(int32_t)distance,origin);release(s);fnv_cpu_return_u32((uint32_t)result,12);}
static void api_get_info(void){uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(flags){bad("MMIO get-info flags pending",flags);return;}unsigned char *out=checked(at,72,1);if(!out)return;struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(5,12);return;}memmove(out,info(s),72);release(s);fnv_cpu_return_u32(0,12);}
static int set_info(struct mmio_slot *s,uint32_t at){unsigned char *in=checked(at,72,0),*p=info(s);if(!in || !p)return 0;
    /* Only consumption inside the current read buffer is mutable. */
    for(unsigned i=0;i<72;i++)if((i<28 || i>=32) && in[i]!=p[i]){bad("MMIO set-info fields pending",at+i);return 0;}
    uint32_t next=get32(in+28);if(next<s->buffer || next>get32(p+32)){bad("MMIO set-info buffer pointer invalid",next);return 0;}put32(p+28,next);return 1;}
static void api_set_info(void){uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(flags){bad("MMIO set-info flags pending",flags);return;}struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(5,12);return;}(void)set_info(s,at);release(s);fnv_cpu_return_u32(0,12);}
static void api_advance(void){uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(flags){bad("MMIO advance write mode pending",flags);return;}unsigned char *out=at?checked(at,72,1):NULL;struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(5,12);return;}if(at && !set_info(s,at))return;int n=fill(s);if(out)memmove(out,info(s),72);release(s);fnv_cpu_return_u32(n<0?261:0,12);}
static void api_descend(void){
 uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),parent=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3);
 if(flags && flags!=16 && flags!=32 && flags!=64){bad("MMIO descend flags pending",flags);return;}
 unsigned char *out=checked(at,20,1),*ancestor=parent?checked(parent,20,0):NULL;if(!out || (parent && !ancestor))return;
 uint32_t wanted=get32(out),type=get32(out+8);uint64_t limit=INT32_MAX;
 if(ancestor){limit=(uint64_t)get32(ancestor+12)+get32(ancestor+4);if(limit>INT32_MAX){bad("MMIO parent chunk range invalid",parent);return;}}
 struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(5,16);return;}uint32_t result=265;
 for(;;){uint32_t pos=position(s);if(pos==UINT32_MAX || (uint64_t)pos+8>limit)break;
    uint32_t scratch=s->buffer+BUFFER_BYTES;unsigned char *bytes=checked(scratch,12,1);if(!bytes)break;
    if(stream_read(s,scratch,8)!=8)break;uint32_t id=get32(bytes),size=get32(bytes+4),form=0;
    uint64_t end=(uint64_t)pos+8+size,padded=end+(size&1u);
    if(end>limit || padded>INT32_MAX)break;
    int container=id==0x46464952u || id==0x5453494cu;
    if(container){if(size<4 || stream_read(s,scratch+8,4)!=4)break;form=get32(bytes+8);}
    int match=!flags || (flags==16 && id==wanted) || (flags==32 && id==0x46464952u && form==type) || (flags==64 && id==0x5453494cu && form==type);
    if(match){put32(out,id);put32(out+4,size);put32(out+8,form);put32(out+12,pos+8);put32(out+16,0);result=0;break;}
    if(padded>limit || stream_seek(s,(int32_t)padded,0)<0)break;
 }
 release(s);fnv_cpu_return_u32(result,16);
}
static void api_ascend(void){
 uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(flags){bad("MMIO ascend flags pending",flags);return;}
 unsigned char *p=checked(at,20,0);if(!p)return;uint32_t size=get32(p+4);uint64_t end=(uint64_t)get32(p+12)+size+(size&1u);
 if(get32(p+16) || end>INT32_MAX){bad("MMIO dirty or overflowing chunk pending",at);return;}
 struct mmio_slot *s=acquire(h);if(!s){fnv_cpu_return_u32(5,12);return;}int32_t r=stream_seek(s,(int32_t)end,0);release(s);fnv_cpu_return_u32(r<0?263:0,12);
}
static const struct fnv_cpu_export exports[]={
 {"winmm.dll","mmioOpenA",0,0,api_open},{"winmm.dll","mmioClose",0,0,api_close},
 {"winmm.dll","mmioRead",0,0,api_read},{"winmm.dll","mmioSeek",0,0,api_seek},
 {"winmm.dll","mmioGetInfo",0,0,api_get_info},{"winmm.dll","mmioSetInfo",0,0,api_set_info},
 {"winmm.dll","mmioAdvance",0,0,api_advance},{"winmm.dll","mmioDescend",0,0,api_descend},
 {"winmm.dll","mmioAscend",0,0,api_ascend}};
const struct fnv_cpu_export *fnv_windows_mmio_exports(size_t *n){if(n)*n=9;return exports;}
enum fnv_cpu_result fnv_windows_mmio_create(struct fnv_windows_process *process,const struct fnv_mmio_config *c,struct fnv_windows_mmio **out){
 if(!out)return FNV_CPU_INVALID;*out=NULL;if(!process || !c || (c->arena_address&63) || c->arena_bytes<0x11000)return FNV_CPU_INVALID;if(active)return FNV_CPU_BUSY;
 void *backing;struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,c->arena_address,c->arena_bytes,&backing);if(r)return r;
 if(c->memory && (!fnv_vm_access(c->memory,c->arena_address,c->arena_bytes,1) || !fnv_vm_retain(c->memory)))return FNV_CPU_INVALID;
 struct fnv_windows_mmio *m=calloc(1,sizeof(*m));if(!m){if(c->memory)fnv_vm_release_reference(c->memory);return FNV_CPU_NO_MEMORY;}
 m->cpu=cpu;m->config=*c;if(!fnv_host_mutex_init(&m->guard)){if(c->memory)fnv_vm_release_reference(c->memory);free(m);return FNV_CPU_NO_MEMORY;}
 for(unsigned i=0;i<SLOT_COUNT;i++){m->slots[i].info=c->arena_address+i*SLOT_STRIDE;m->slots[i].buffer=m->slots[i].info+80;}
 fnv_cpu_function f[]={api_open,api_close,api_read,api_seek,api_get_info,api_set_info,api_advance,api_descend,api_ascend};r=fnv_cpu_register_native(cpu,f,9,m->tokens);
 if(r){fnv_host_mutex_destroy(&m->guard);if(c->memory)fnv_vm_release_reference(c->memory);free(m);return r;}active=m;*out=m;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_mmio_destroy(struct fnv_windows_mmio *m){if(!m || m!=active)return FNV_CPU_INVALID;for(unsigned i=0;i<SLOT_COUNT;i++)if(m->slots[i].phase)return FNV_CPU_BUSY;
 enum fnv_cpu_result r=fnv_cpu_unregister_native(m->cpu,m->tokens[0],9);if(r)return r;active=NULL;fnv_host_mutex_destroy(&m->guard);if(m->config.memory)fnv_vm_release_reference(m->config.memory);free(m);return FNV_CPU_OK;}
