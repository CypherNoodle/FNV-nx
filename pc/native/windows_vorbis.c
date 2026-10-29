/* Windows32 Vorbisfile ABI over native Xiph decoding and translated cdecl I/O. */
#include "fnv_sdk_runtime.h"
#include "windows_vorbis.h"
#include "guest_memory.h"
#include "host_mutex.h"
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#define STREAMS 128u
#define STRIDE 0x11000u
#define SCRATCH 65536u
struct stream {uint32_t key,phase,source,functions[4],scratch,info;OggVorbis_File file;const vorbis_info *views[8];};
struct fnv_windows_vorbis {struct fnv_cpu_runtime *cpu;struct fnv_vorbis_config config;fnv_host_mutex guard;uint32_t tokens[6];struct stream streams[STREAMS];};
static struct fnv_windows_vorbis *active;
static atomic_uint_fast64_t metric_opens,metric_open_failures,metric_reads,metric_decoded;
static atomic_int metric_last_error;
static atomic_uint_fast64_t metric_attempts,metric_capacity_denials,metric_live,metric_peak;
int fnv_windows_vorbis_metrics(struct fnv_vorbis_metrics *out){
    if(!out || !active)return 0;
    *out=(struct fnv_vorbis_metrics){atomic_load_explicit(&metric_opens,memory_order_relaxed),atomic_load_explicit(&metric_open_failures,memory_order_relaxed),atomic_load_explicit(&metric_reads,memory_order_relaxed),atomic_load_explicit(&metric_decoded,memory_order_relaxed),atomic_load_explicit(&metric_last_error,memory_order_relaxed),atomic_load_explicit(&metric_attempts,memory_order_relaxed),atomic_load_explicit(&metric_capacity_denials,memory_order_relaxed),atomic_load_explicit(&metric_live,memory_order_relaxed),atomic_load_explicit(&metric_peak,memory_order_relaxed)};return 1;
}
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static void bad(const char *s,uint32_t at){recomp_unimpl(s,at);}
static unsigned char *checked(uint32_t at,uint32_t n,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,n):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *backing;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&backing,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))p=NULL;}
    if(!p)bad("Vorbis guest memory unavailable",at);
    return p;
}
static int ready(void){if(active && active->cpu==fnv_cpu_current())return 1;bad("Vorbis provider unavailable",0);return 0;}
static uint32_t call(struct stream *s,unsigned kind,const uint32_t *args,unsigned count){
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || state.gpr[4]<low || state.gpr[4]>high || state.gpr[4]-low<256 || (state.gpr[4]&3) || !s->functions[kind]){
        bad("Vorbis callback stack or entry unavailable",s->functions[kind]);return 0;}
    uint32_t frame=state.gpr[4]-256;unsigned char *p=checked(frame,4+count*4,1),*ret=checked(state.gpr[4],4,0);
    if(!p || !ret)return 0;
    wr(p,rd(ret));for(unsigned i=0;i<count;i++)wr(p+4+i*4,args[i]);
    return fnv_cpu_invoke_cdecl(s->functions[kind],frame,count*4);
}
static size_t read_cb(void *ptr,size_t size,size_t count,void *context){
    struct stream *s=context;if(!size || !count)return 0;
    if(size>SCRATCH || count>SIZE_MAX/size){bad("Vorbis callback read extent invalid",s->key);return 0;}
    size_t done=0;unsigned char *scratch=checked(s->scratch,SCRATCH,1);if(!scratch)return 0;
    while(done<count){size_t n=count-done;if(n>SCRATCH/size)n=SCRATCH/size;
        uint32_t args[]={s->scratch,(uint32_t)size,(uint32_t)n,s->source};uint32_t got=call(s,0,args,4);
        if(got>n){bad("Vorbis callback read count invalid",s->key);return 0;}
        memcpy((unsigned char*)ptr+done*size,scratch,(size_t)got*size);done+=got;if(got<n)break;
    }return done;
}
static int seek_cb(void *context,ogg_int64_t offset,int origin){struct stream *s=context;uint64_t bits=(uint64_t)offset;uint32_t args[]={s->source,(uint32_t)bits,(uint32_t)(bits>>32),(uint32_t)origin};return (int32_t)call(s,1,args,4);}
static int close_cb(void *context){struct stream *s=context;return (int32_t)call(s,2,&s->source,1);}
static long tell_cb(void *context){struct stream *s=context;return (int32_t)call(s,3,&s->source,1);}
static struct stream *acquire(uint32_t key){
    if(!ready())return NULL;
    struct stream *s=NULL;int busy=0;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<active->config.streams;i++)if(active->streams[i].key==key && active->streams[i].phase){
        if(active->streams[i].phase==2){s=active->streams+i;s->phase=3;}else busy=1;break;}
    fnv_host_mutex_unlock(&active->guard);if(busy)bad("Vorbis concurrent or recursive stream access",key);return s;
}
static void release(struct stream *s){fnv_host_mutex_lock(&active->guard);s->phase=2;fnv_host_mutex_unlock(&active->guard);}
static void api_open(void){
    uint32_t source=fnv_cpu_argument_u32(0),key=fnv_cpu_argument_u32(1),initial=fnv_cpu_argument_u32(2),bytes=fnv_cpu_argument_u32(3),functions[4];
    if(!ready())return;
    atomic_fetch_add_explicit(&metric_attempts,1,memory_order_relaxed);
    for(unsigned i=0;i<4;i++)functions[i]=fnv_cpu_argument_u32(4+i);
    if(bytes>1048576 || (!initial && bytes) || !functions[0] || (!!functions[1]!=!!functions[3])){fnv_cpu_return_u32((uint32_t)OV_EINVAL,0);return;}
    (void)checked(key,4,1);unsigned char *input=bytes?checked(initial,bytes,0):NULL;
    struct stream *s=NULL;int duplicate=0;fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<active->config.streams;i++)if(active->streams[i].phase && active->streams[i].key==key)duplicate=1;
    if(!duplicate)for(unsigned i=0;i<active->config.streams;i++)if(!active->streams[i].phase){s=active->streams+i;s->phase=1;s->key=key;break;}
    fnv_host_mutex_unlock(&active->guard);if(duplicate){bad("Vorbis file already active",key);return;}if(!s){
        atomic_fetch_add_explicit(&metric_capacity_denials,1,memory_order_relaxed);atomic_fetch_add_explicit(&metric_open_failures,1,memory_order_relaxed);
        atomic_store_explicit(&metric_last_error,OV_EFAULT,memory_order_relaxed);fnv_cpu_return_u32((uint32_t)OV_EFAULT,0);return;}
    uint_fast64_t live=atomic_fetch_add_explicit(&metric_live,1,memory_order_relaxed)+1,peak=atomic_load_explicit(&metric_peak,memory_order_relaxed);
    while(live>peak && !atomic_compare_exchange_weak_explicit(&metric_peak,&peak,live,memory_order_relaxed,memory_order_relaxed)){}
    s->source=source;memcpy(s->functions,functions,sizeof(functions));memset(&s->file,0,sizeof(s->file));memset(s->views,0,sizeof(s->views));
    ov_callbacks cb={read_cb,functions[1]?seek_cb:NULL,functions[2]?close_cb:NULL,functions[3]?tell_cb:NULL};
    int result=ov_open_callbacks(s,&s->file,(const char*)input,(long)bytes,cb);
    atomic_fetch_add_explicit(&metric_opens,1,memory_order_relaxed);if(result){atomic_fetch_sub_explicit(&metric_live,1,memory_order_relaxed);atomic_fetch_add_explicit(&metric_open_failures,1,memory_order_relaxed);atomic_store_explicit(&metric_last_error,result,memory_order_relaxed);}
    fnv_host_mutex_lock(&active->guard);s->phase=result?0:2;fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32((uint32_t)result,0);
}
static void api_clear(void){uint32_t key=fnv_cpu_argument_u32(0);struct stream *s=acquire(key);if(!s){fnv_cpu_return_u32((uint32_t)OV_EINVAL,0);return;}
    int result=ov_clear(&s->file);atomic_fetch_sub_explicit(&metric_live,1,memory_order_relaxed);fnv_host_mutex_lock(&active->guard);s->phase=0;fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32((uint32_t)result,0);}
static void api_total(void){uint32_t key=fnv_cpu_argument_u32(0);int link=(int32_t)fnv_cpu_argument_u32(1);struct stream *s=acquire(key);
    ogg_int64_t result=s?ov_pcm_total(&s->file,link):OV_EINVAL;if(s)release(s);fnv_cpu_return_u64((uint64_t)result,0);}
static void api_seekable(void){struct stream *s=acquire(fnv_cpu_argument_u32(0));int result=s?ov_seekable(&s->file):0;if(s)release(s);fnv_cpu_return_u32((uint32_t)result,0);}
static void api_read(void){
    uint32_t key=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),n=fnv_cpu_argument_u32(2),endian=fnv_cpu_argument_u32(3),word=fnv_cpu_argument_u32(4),sign=fnv_cpu_argument_u32(5),section=fnv_cpu_argument_u32(6);
    if(!n || n>1048576 || endian>1 || (word!=1 && word!=2) || sign>1){fnv_cpu_return_u32((uint32_t)OV_EINVAL,0);return;}
    unsigned char *p=checked(at,n,1),*out=section?checked(section,4,1):NULL;struct stream *s=acquire(key);if(!s){fnv_cpu_return_u32((uint32_t)OV_EINVAL,0);return;}
    int bitstream=0;long result=ov_read(&s->file,(char*)p,(int)n,(int)endian,(int)word,(int)sign,&bitstream);if(out && result>0)wr(out,(uint32_t)bitstream);
    atomic_fetch_add_explicit(&metric_reads,1,memory_order_relaxed);if(result>0)atomic_fetch_add_explicit(&metric_decoded,(uint64_t)result,memory_order_relaxed);else if(result<0)atomic_store_explicit(&metric_last_error,(int)result,memory_order_relaxed);
    release(s);fnv_cpu_return_u32((uint32_t)result,0);
}
static void api_info(void){
    uint32_t key=fnv_cpu_argument_u32(0);int link=(int32_t)fnv_cpu_argument_u32(1);struct stream *s=acquire(key);if(!s){fnv_cpu_return_u32(0,0);return;}
    vorbis_info *v=ov_info(&s->file,link);uint32_t result=0;
    if(v){unsigned i;for(i=0;i<8 && s->views[i] && s->views[i]!=v;i++){}if(i==8){release(s);bad("Vorbis info view capacity exceeded",key);return;}
        s->views[i]=v;result=s->info+i*32;unsigned char *p=checked(result,32,1);
        int64_t values[7]={v->version,v->channels,v->rate,v->bitrate_upper,v->bitrate_nominal,v->bitrate_lower,v->bitrate_window};
        for(unsigned k=0;k<7;k++){if(values[k]<INT32_MIN || values[k]>INT32_MAX){release(s);bad("Vorbis Windows32 info field overflow",key);return;}wr(p+k*4,(uint32_t)values[k]);}wr(p+28,0);
    }release(s);fnv_cpu_return_u32(result,0);
}
static const struct fnv_cpu_export exports[]={
 {"libvorbisfile.dll","ov_open_callbacks",0,0,api_open},{"libvorbisfile.dll","ov_clear",0,0,api_clear},
 {"libvorbisfile.dll","ov_pcm_total",0,0,api_total},{"libvorbisfile.dll","ov_seekable",0,0,api_seekable},
 {"libvorbisfile.dll","ov_read",0,0,api_read},{"libvorbisfile.dll","ov_info",0,0,api_info}};
const struct fnv_cpu_export *fnv_windows_vorbis_exports(size_t *n){if(n)*n=6;return exports;}
enum fnv_cpu_result fnv_windows_vorbis_create(struct fnv_windows_process *process,const struct fnv_vorbis_config *c,struct fnv_windows_vorbis **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!process || !c || !c->streams || c->streams>STREAMS || (c->arena_address&63) || c->arena_bytes<(uint64_t)c->streams*STRIDE)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);void *backing;enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,c->arena_address,c->arena_bytes,&backing);if(r)return r;
    if(c->memory && (!fnv_vm_access(c->memory,c->arena_address,c->arena_bytes,1) || !fnv_vm_retain(c->memory)))return FNV_CPU_INVALID;
    struct fnv_windows_vorbis *m=calloc(1,sizeof(*m));if(!m){if(c->memory)fnv_vm_release_reference(c->memory);return FNV_CPU_NO_MEMORY;}
    m->cpu=cpu;m->config=*c;if(!fnv_host_mutex_init(&m->guard)){if(c->memory)fnv_vm_release_reference(c->memory);free(m);return FNV_CPU_NO_MEMORY;}
    for(unsigned i=0;i<c->streams;i++){m->streams[i].info=c->arena_address+i*STRIDE;m->streams[i].scratch=m->streams[i].info+4096;}
    fnv_cpu_function functions[]={api_open,api_clear,api_total,api_seekable,api_read,api_info};r=fnv_cpu_register_native(cpu,functions,6,m->tokens);
    if(r){fnv_host_mutex_destroy(&m->guard);if(c->memory)fnv_vm_release_reference(c->memory);free(m);return r;}active=m;*out=m;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_vorbis_destroy(struct fnv_windows_vorbis *m){if(!m || m!=active)return FNV_CPU_INVALID;
    for(unsigned i=0;i<m->config.streams;i++)if(m->streams[i].phase)return FNV_CPU_BUSY;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(m->cpu,m->tokens[0],6);if(r)return r;active=NULL;fnv_host_mutex_destroy(&m->guard);if(m->config.memory)fnv_vm_release_reference(m->config.memory);free(m);return FNV_CPU_OK;}
