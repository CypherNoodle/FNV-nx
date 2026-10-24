/* Own closed-profile Bink guest bridge, backed by real decoding and PCM mixing. */
#include "fnv_sdk_runtime.h"
#include "bink_movie.h"
#include "bink_stream.h"
#include "bink_copy.h"
#include "dsound8.h"
#include "guest_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
struct fnv_bink_movie {
    struct fnv_cpu_runtime *cpu;struct fnv_bink_movie_config config;
    struct fnv_bink_sound *selection;uint32_t tokens[8],used,handle,header[7];
    struct fnv_bink_io io;struct fnv_bink_stream *stream;struct fnv_bink_info info;
    struct fnv_bink_frame frame;uint64_t epoch,pause_at,last_now;
    int opened,opening,paused,decoded;
    atomic_uint owner;
};
static struct fnv_bink_movie *active;
static void fail(const char *s,uint32_t at){recomp_unimpl(s,at);}
static unsigned char *memory(uint32_t at,uint32_t n,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,n):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&data,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))p=NULL;}
    if(!p)fail("Bink movie memory unavailable",at);
    return p;
}
static int current(void){
    if(active && active->cpu==fnv_cpu_current()){
        uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)low;(void)high;
        unsigned expected=0;
        if(id && (atomic_compare_exchange_strong_explicit(&active->owner,&expected,id,memory_order_relaxed,memory_order_relaxed) || expected==id))return 1;
        fail("Bink movie controller thread unavailable",id);return 0;
    }fail("Bink movie provider unavailable",0);return 0;
}
static int now(uint64_t *n){
    if(!active->config.now_ns(active->config.context,n) || *n<active->last_now){fail("Bink movie clock unavailable or reversed",0);return 0;}
    active->last_now=*n;return 1;
}
static int handle(uint32_t h){
    if(!current())return 0;
    if(!active->stream || h!=active->handle || active->opening){fail("Bink movie invalid handle",h);return 0;}
    unsigned char *p=memory(h,28,0);if(!p)return 0;
    if(memcmp(p,active->header,28)){fail("Bink movie guest header modified",h);return 0;}return 1;
}
static int publish(void){unsigned char *p=memory(active->handle,28,1);if(!p)return 0;memcpy(p,active->header,28);return 1;}
static void close_stream(struct fnv_bink_movie *m){
    if(m->stream){fnv_bink_stream_close(m->stream);m->stream=NULL;}
    if(m->opened){m->config.close_io(m->config.context,&m->io);m->opened=0;}
    memset(&m->frame,0,sizeof(m->frame));m->handle=0;m->opening=m->paused=m->decoded=0;
}
static uint32_t invoke(uint32_t entry,const uint32_t *args){
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || (state.gpr[4]&3) || state.gpr[4]>high || state.gpr[4]<low || state.gpr[4]-low<160){fail("Bink movie sound callback stack unavailable",entry);return 0;}
    unsigned char *p=memory(state.gpr[4]-160,28,1),*ret=memory(state.gpr[4],4,0);if(!p || !ret)return 0;
    memcpy(p,ret,4);memcpy(p+4,args,24);return fnv_cpu_invoke_stdcall(entry,state.gpr[4]-160,24);
}
/* Private opener: both sides belong to this provider. No foreign BINKSND layout
 * is fabricated. It performs actual decoder/mixer initialization, not a stub. */
static void api_sound_open(void){
    if(!current())return;
    if(!active->opening || !active->opened || active->stream || fnv_cpu_argument_u32(0)!=active->handle ||
       fnv_cpu_argument_u32(1)!=48000 || fnv_cpu_argument_u32(2)!=16 || fnv_cpu_argument_u32(3)!=2 ||
       fnv_cpu_argument_u32(4)!=0 || fnv_cpu_argument_u32(5)!=active->handle){fail("Bink private sound opener contract unavailable",0);return;}
    int ok=fnv_bink_stream_open(&active->io,active->config.sound,&active->stream,&active->info);
    fnv_cpu_return_u32((uint32_t)ok,24);
}
uint32_t fnv_bink_movie_sound_factory(void *context,uint32_t device){
    struct fnv_bink_movie *m=context;struct fnv_dsound_movie_stats stats;
    if(!m || m!=active || device || fnv_dsound8_movie_stats(m->config.sound,&stats)!=FNV_PCM_OK)return 0;
    return m->tokens[7];
}
static void api_open(void){
    uint32_t path=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1);if(!current())return;
    if(active->stream || active->opening || (flags&~0x08002000u)){fail("Bink concurrent movie or open flags unavailable",flags);return;}
    struct fnv_bink_sound_state selected;
    if(!active->selection || fnv_bink_sound_state(active->selection,&selected)!=FNV_CPU_OK || selected.open!=active->tokens[7]){fail("Bink movie sound system unavailable",0);return;}
    char name[512];unsigned n;for(n=0;n<sizeof(name);n++){if(path>UINT32_MAX-n){fail("Bink movie path overflow",path);return;}unsigned char *p=memory(path+n,1,0);if(!p)return;name[n]=(char)*p;if(!name[n])break;}
    if(!n || n==sizeof(name)){fail("Bink movie path unavailable",path);return;}
    if(active->used>=active->config.arena_bytes/64){fnv_cpu_return_u32(0,8);return;}
    uint64_t size=0;memset(&active->io,0,sizeof(active->io));
    if(!active->config.open_io(active->config.context,name,&active->io,&size)){fnv_cpu_return_u32(0,8);return;}
    active->opened=1;active->handle=active->config.arena_address+active->used++*64;active->opening=1;
    uint32_t args[]={active->handle,48000,16,2,0,active->handle};uint32_t ok=invoke(selected.open,args);active->opening=0;
    if(!ok || !active->stream || size<8 || size>UINT32_MAX){close_stream(active);fnv_cpu_return_u32(0,8);return;}
    if((__uint128_t)active->info.frames*active->info.rate_den*1000000000u/active->info.rate_num>UINT64_MAX){close_stream(active);fail("Bink movie duration overflow",0);return;}
    uint32_t fields[]={active->info.width,active->info.height,active->info.frames,1,UINT32_MAX,active->info.rate_num,active->info.rate_den};memcpy(active->header,fields,sizeof(fields));
    if(!now(&active->epoch) || !publish())return;
    active->paused=active->decoded=0;fnv_cpu_return_u32(active->handle,8);
}
static void api_close(void){uint32_t h=fnv_cpu_argument_u32(0);if(!handle(h))return;close_stream(active);fnv_cpu_return_u32(0,4);}
static void api_wait(void){
    uint32_t h=fnv_cpu_argument_u32(0);if(!handle(h))return;uint64_t t;if(!now(&t))return;
    uint64_t due=(uint64_t)((__uint128_t)(active->header[3]-1)*active->info.rate_den*1000000000u/active->info.rate_num);
    fnv_cpu_return_u32(active->paused || t-active->epoch<due,4);
}
static void api_pause(void){
    uint32_t h=fnv_cpu_argument_u32(0),paused=fnv_cpu_argument_u32(1);if(!handle(h))return;
    if(paused>1){fail("Bink pause state unavailable",paused);return;}uint64_t t;if(!now(&t))return;
    if(paused!=(uint32_t)active->paused){
        if(!fnv_bink_stream_pause(active->stream,(int)paused)){fail("Bink movie pause failed",h);return;}
        if(paused)active->pause_at=t;else {uint64_t delta=t-active->pause_at;if(active->epoch>UINT64_MAX-delta){fail("Bink pause clock overflow",h);return;}active->epoch+=delta;}
        active->paused=(int)paused;
    }fnv_cpu_return_u32(paused,8);
}
static void api_decode(void){
    uint32_t h=fnv_cpu_argument_u32(0);if(!handle(h))return;
    if(active->decoded){fnv_cpu_return_u32(0,4);return;}
    if(active->paused){fail("Bink decoding while paused unavailable",h);return;}
    uint64_t start;if(!now(&start))return;
    for(unsigned i=0;i<4096;i++){
        enum fnv_bink_stream_result r=fnv_bink_stream_step(active->stream,&active->frame);
        if(r==FNV_BINK_STREAM_VIDEO){active->decoded=1;active->header[4]=active->header[3];if(publish())fnv_cpu_return_u32(0,4);return;}
        if(r==FNV_BINK_STREAM_WAIT){uint64_t t;if(!now(&t))return;if(t-start>=2000000000u || !active->config.wait_audio(active->config.context)){fail("Bink movie audio cannot progress",h);return;}}
        else if(r!=FNV_BINK_STREAM_AUDIO){fail("Bink movie decoding failed or unexpected EOF",h);return;}
    }fail("Bink movie decode budget exceeded",h);
}
static void api_next(void){
    uint32_t h=fnv_cpu_argument_u32(0);if(!handle(h))return;
    if(active->paused){fail("Bink advancing while paused unavailable",h);return;}
    if(!active->decoded){fail("Bink skipping undecoded frame unavailable",h);return;}
    active->decoded=0;memset(&active->frame,0,sizeof(active->frame));
    if(active->header[3]==active->header[2]){
        if(!fnv_bink_stream_close(active->stream)){active->stream=NULL;fail("Bink movie loop reset failed",h);return;}active->stream=NULL;
        if(active->io.seek(active->io.context,0,0)<0 || !fnv_bink_stream_open(&active->io,active->config.sound,&active->stream,&active->info)){fail("Bink movie rewind failed",h);return;}
        active->header[3]=1;if(!now(&active->epoch))return;
    }else active->header[3]++;
    if(publish())fnv_cpu_return_u32(0,4);
}
static void api_copy(void){
    uint32_t a[11];for(unsigned i=0;i<11;i++)a[i]=fnv_cpu_argument_u32(i);if(!handle(a[0]))return;
    if(!active->decoded){fail("Bink copy before decoded frame unavailable",a[0]);return;}
    uint64_t span=(uint64_t)a[2]*a[3];if(!span || span>UINT32_MAX || (int32_t)a[2]<=0){fail("Bink copy destination span unavailable",a[1]);return;}
    unsigned char *p=memory(a[1],(uint32_t)span,1);if(!p)return;
    enum fnv_bink_copy_result r=fnv_bink_copy_rect(&active->frame,(size_t)active->frame.pitch*active->frame.height,p,(size_t)span,(int32_t)a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10]);
    if(r!=FNV_BINK_COPY_OK){fail("Bink copy profile or bounds unavailable",a[1]);return;}fnv_cpu_return_u32(0,44);
}
static const struct fnv_cpu_export exports[]={
    {"binkw32.dll","_BinkOpen@8",0,0,api_open},{"binkw32.dll","_BinkClose@4",0,0,api_close},
    {"binkw32.dll","_BinkWait@4",0,0,api_wait},{"binkw32.dll","_BinkPause@8",0,0,api_pause},
    {"binkw32.dll","_BinkDoFrame@4",0,0,api_decode},{"binkw32.dll","_BinkNextFrame@4",0,0,api_next},
    {"binkw32.dll","_BinkCopyToBufferRect@44",0,0,api_copy}};
const struct fnv_cpu_export *fnv_bink_movie_exports(size_t *n){if(n)*n=7;return exports;}
enum fnv_cpu_result fnv_bink_movie_create(struct fnv_windows_process *process,const struct fnv_bink_movie_config *cfg,struct fnv_bink_movie **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;void *backing;struct fnv_dsound_movie_stats stats;
    if(!process || !cfg || !cfg->open_io || !cfg->close_io || !cfg->now_ns || !cfg->wait_audio || (cfg->arena_address&63) || cfg->arena_bytes<4096 || cfg->arena_bytes>65536 || fnv_dsound8_movie_stats(cfg->sound,&stats)!=FNV_PCM_OK)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);
    enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,cfg->arena_address,cfg->arena_bytes,&backing);if(r)return r;
    struct fnv_bink_movie *m=calloc(1,sizeof(*m));if(!m)return FNV_CPU_NO_MEMORY;atomic_init(&m->owner,0);m->cpu=cpu;m->config=*cfg;
    fnv_cpu_function functions[]={api_open,api_close,api_wait,api_pause,api_decode,api_next,api_copy,api_sound_open};
    r=fnv_cpu_register_native(cpu,functions,8,m->tokens);if(r){free(m);return r;}active=m;*out=m;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_bink_movie_bind_sound(struct fnv_bink_movie *m,struct fnv_bink_sound *s){
    struct fnv_bink_sound_state state;if(!m || m!=active || m->stream || fnv_bink_sound_state(s,&state)!=FNV_CPU_OK)return FNV_CPU_INVALID;m->selection=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_bink_movie_destroy(struct fnv_bink_movie *m){
    if(!m || m!=active)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(m->cpu,m->tokens[0],8);if(r)return r;
    close_stream(m);active=NULL;free(m);return FNV_CPU_OK;
}
