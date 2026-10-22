/* Original bounded DirectSound8 PCM bridge, no Windows DLL implementation. */
#include "fnv_sdk_runtime.h"
#include "dsound8.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define INVALID 0x80070057u
#define NOMEM 0x8007000eu
#define CONTROL 0x8878001eu
#define INVALIDCALL 0x88780032u
#define CAPACITY 256u
#define METHODS 36u
struct sound_object {
    uint32_t refs,kind,parent,flags,bytes,address,rate,frequency,channels,bits;
    uint32_t cooperative,playing,looping,locked,offset,first,second;
    int32_t volume,pan,gain[2];uint64_t phase;
    unsigned char *samples;
};
struct fnv_dsound8 {
    struct fnv_cpu_runtime *cpu;struct fnv_dsound8_config config;
    fnv_host_mutex guard;unsigned char *memory;
    uint32_t tokens[METHODS],count,used,focused;struct sound_object objects[CAPACITY];
};
static struct fnv_dsound8 *active;
static const unsigned char unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
static const unsigned char device[16]={0x93,0x7e,0x0a,0xc5,0x95,0xf3,0x34,0x48,0x9e,0xf6,0x7f,0xa9,0x9d,0xe5,9,0x66};
static const unsigned char legacy_device[16]={0x83,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
static const unsigned char buffer[16]={0x85,0xfa,0x9a,0x27,0x81,0x49,0xce,0x11,0xa5,0x21,0,0x20,0xaf,0x0b,0xe5,0x60};
static const unsigned char buffer8[16]={0x49,0xa4,0x25,0x68,0x24,0x75,0x82,0x4d,0x92,0x0f,0x50,0xe3,0x6a,0xb3,0xab,0x1e};
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t half(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8;}
static void wr(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static unsigned char *range(uint32_t at,uint32_t n,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,n):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,bytes;
        if(!fnv_vm_binding(vm,&data,&base,&bytes) || ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))p=NULL;}
    if(!p)recomp_unimpl("DirectSound inaccessible guest buffer",at);
    return p;
}
static int current(void){if(active && active->cpu==fnv_cpu_current())return 1;recomp_unimpl("DirectSound provider unavailable",0);return 0;}
static uint32_t address(unsigned i){return active->config.arena_address+512+i*8;}
static int index_of(uint32_t self){
    if(self<active->config.arena_address+512 || (self-active->config.arena_address-512)%8)return -1;
    uint32_t i=(self-active->config.arena_address-512)/8;return i<active->count && active->objects[i].refs?(int)i:-1;
}
/* Returns with mutex held only on success. Never unwind a held host mutex. */
static struct sound_object *acquire(uint32_t self,int kind){
    if(!current())return NULL;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i>=0 && (kind<0 || active->objects[i].kind==(unsigned)kind))return active->objects+i;
    fnv_host_mutex_unlock(&active->guard);recomp_unimpl("DirectSound stale or mismatched COM identity",self);return NULL;
}
static void finish(uint32_t hr,unsigned bytes){fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,bytes);}
static uint32_t allocate(unsigned kind,unsigned parent){
    if(active->count==CAPACITY)return 0;
    unsigned i=active->count++;struct sound_object *o=active->objects+i;
    o->refs=1;o->kind=kind;o->parent=parent;o->gain[0]=o->gain[1]=65536;
    o->rate=o->frequency=48000;o->channels=2;o->bits=16;
    if(parent)active->objects[parent-1].refs++;
    wr(active->memory+512+i*8,active->config.arena_address+(kind?48:0));return address(i);
}
static void factory(void){
    uint32_t guid=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1),outer=fnv_cpu_argument_u32(2);
    if(!current())return;
    unsigned char *p=range(out,4,1);if(!p)return;wr(p,0);
    if(outer){fnv_cpu_return_u32(0x80040110u,12);return;}
    if(guid){recomp_unimpl("DirectSound named device selection pending",guid);return;}
    fnv_host_mutex_lock(&active->guard);uint32_t result=allocate(0,0);wr(p,result);finish(result?0:NOMEM,12);
}
static void query(void){
    uint32_t self=fnv_cpu_argument_u32(0),iid=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2);
    unsigned char *g=range(iid,16,0),*p=range(out,4,1);if(!g || !p)return;
    unsigned char id[16];memcpy(id,g,16);wr(p,0);struct sound_object *o=acquire(self,-1);if(!o)return;
    int ok=!memcmp(id,unknown,16) || (!o->kind && (!memcmp(id,device,16)||!memcmp(id,legacy_device,16))) ||
        (o->kind && (!memcmp(id,buffer,16)||(!(o->flags&1) && !memcmp(id,buffer8,16))));
    uint32_t hr=0x80004002u;
    if(ok){if(o->refs==UINT32_MAX)hr=NOMEM;else {o->refs++;wr(p,self);hr=0;}}
    finish(hr,12);
}
static void ref(int add){
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),-1);if(!o)return;
    if(add && o->refs==UINT32_MAX){fnv_host_mutex_unlock(&active->guard);recomp_unimpl("DirectSound COM references exhausted",0);return;}
    uint32_t n=add?++o->refs:--o->refs;
    if(!n){o->playing=0;free(o->samples);o->samples=NULL;if(o->parent)active->objects[o->parent-1].refs--;}
    finish(n,4);
}
static void addref(void){ref(1);}static void release(void){ref(0);}
static int format(const unsigned char *p,uint32_t *rate,uint32_t *channels,uint32_t *bits){
    *channels=half(p+2);*rate=rd(p+4);*bits=half(p+14);
    return half(p)==1 && (*channels==1||*channels==2) && (*bits==8||*bits==16) && *rate>=8000 && *rate<=48000 &&
        half(p+12)==*channels*(*bits/8) && rd(p+8)==*rate*half(p+12) && !half(p+16);
}
static void create_buffer(void){
    uint32_t self=fnv_cpu_argument_u32(0),desc=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2),outer=fnv_cpu_argument_u32(3);
    unsigned char *p=range(out,4,1),*d=range(desc,36,0);if(!p||!d)return;
    uint32_t size=rd(d),flags=rd(d+4),bytes=rd(d+8),reserved=rd(d+12),fmt=rd(d+16),rate=48000,channels=2,bits=16;
    wr(p,0);if(!current())return;
    if(outer){fnv_cpu_return_u32(0x80040110u,16);return;}
    if(size!=36 || reserved){fnv_cpu_return_u32(INVALID,16);return;}
    /* No hardware voices, 3D, effects or notifications are advertised. */
    if(flags&~0x180ebu){recomp_unimpl("DirectSound buffer capabilities pending",flags);return;}
    if(flags&1){if(bytes||fmt){fnv_cpu_return_u32(INVALID,16);return;}}
    else {unsigned char *f=range(fmt,18,0);if(!f)return;
        if(!format(f,&rate,&channels,&bits)){recomp_unimpl("DirectSound PCM format pending",fmt);return;}
        if(bytes<4 || bytes%(channels*(bits/8))){fnv_cpu_return_u32(INVALID,16);return;}}
    if(bytes>active->config.arena_bytes-4096){fnv_cpu_return_u32(NOMEM,16);return;}
    unsigned char *samples=bytes?malloc(bytes):NULL;if(bytes && !samples){fnv_cpu_return_u32(NOMEM,16);return;}
    if(bytes)memset(samples,bits==8?128:0,bytes);
    struct sound_object *root=acquire(self,0);if(!root){free(samples);return;}
    if(flags&1)for(unsigned i=0;i<active->count;i++){struct sound_object *o=active->objects+i;
        if(o->refs && o->parent==(uint32_t)(root-active->objects)+1 && (o->flags&1)){
            if(o->refs==UINT32_MAX){free(samples);finish(NOMEM,16);return;}
            o->refs++;wr(p,address(i));free(samples);finish(0,16);return;}}
    if(bytes>active->config.arena_bytes-active->used){free(samples);finish(NOMEM,16);return;}
    uint32_t at=allocate(1,(unsigned)(root-active->objects)+1);
    if(!at){free(samples);finish(NOMEM,16);return;}
    struct sound_object *o=active->objects+active->count-1;o->flags=flags;o->bytes=bytes;o->rate=o->frequency=rate;o->channels=channels;o->bits=bits;o->samples=samples;
    if(bytes){o->address=active->config.arena_address+active->used;active->used+=bytes;}
    wr(p,at);finish(0,16);
}
static void device_caps(void){
    unsigned char *p=range(fnv_cpu_argument_u32(1),96,1);if(!p)return;
    uint32_t size=rd(p);struct sound_object *o=acquire(fnv_cpu_argument_u32(0),0);if(!o)return;
    if(size!=96){finish(INVALID,8);return;}
    memset(p,0,96);wr(p,96);wr(p+4,0xf3fu);wr(p+8,8000);wr(p+12,48000);wr(p+16,1);finish(0,8);
}
static void cooperative(void){
    uint32_t self=fnv_cpu_argument_u32(0),hwnd=fnv_cpu_argument_u32(1),level=fnv_cpu_argument_u32(2);if(!current())return;
    if(level<1 || level>4 || !hwnd){fnv_cpu_return_u32(INVALID,12);return;}
    if(!active->config.valid_window){recomp_unimpl("DirectSound native window validation unavailable",hwnd);return;}
    if(!active->config.valid_window(active->config.context,hwnd)){fnv_cpu_return_u32(INVALID,12);return;}
    if(level==4){recomp_unimpl("DirectSound write-primary mode pending",level);return;}
    struct sound_object *o=acquire(self,0);if(!o)return;o->cooperative=level;finish(0,12);
}
static void speakers(void){unsigned char *p=range(fnv_cpu_argument_u32(1),4,1);if(!p)return;
    if(!acquire(fnv_cpu_argument_u32(0),0))return;
    wr(p,4);finish(0,8);}
static void certification(void){unsigned char *p=range(fnv_cpu_argument_u32(1),4,1);if(!p)return;
    if(!acquire(fnv_cpu_argument_u32(0),0))return;
    wr(p,1);finish(0,8);}
static void initialize(void){if(!acquire(fnv_cpu_argument_u32(0),0))return;finish(0x88780082u,8);}
static void buffer_caps(void){unsigned char *p=range(fnv_cpu_argument_u32(1),20,1);if(!p)return;uint32_t size=rd(p);
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    if(size!=20){finish(INVALID,8);return;}memset(p,0,20);wr(p,20);wr(p+4,(o->flags&~4u)|8u);wr(p+8,o->bytes);finish(0,8);}
static void position(void){uint32_t a=fnv_cpu_argument_u32(1),b=fnv_cpu_argument_u32(2);
    unsigned char *p=a?range(a,4,1):NULL,*q=b?range(b,4,1):NULL;if((a&&!p)||(b&&!q))return;
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    if(o->flags&1){fnv_host_mutex_unlock(&active->guard);recomp_unimpl("DirectSound primary cursor pending",fnv_cpu_argument_u32(0));return;}
    uint32_t block=o->channels*(o->bits/8),cursor=(uint32_t)(o->phase/48000)*block;
    if(p)wr(p,cursor);
    if(q)wr(q,o->bytes?(cursor+512*block)%o->bytes:0);
    finish(0,12);
}
static void get_format(void){uint32_t at=fnv_cpu_argument_u32(1),size=fnv_cpu_argument_u32(2),written=fnv_cpu_argument_u32(3);
    unsigned char *p=at?range(at,size<18?size:18,1):NULL,*n=written?range(written,4,1):NULL;if((at&&!p)||(written&&!n))return;
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    if(n)wr(n,18);
    if(at && size<18){finish(INVALID,16);return;}
    if(p){memset(p,0,18);p[0]=1;p[2]=(unsigned char)o->channels;wr(p+4,o->rate);wr(p+8,o->rate*o->channels*(o->bits/8));p[12]=(unsigned char)(o->channels*(o->bits/8));p[14]=(unsigned char)o->bits;}
    finish(0,16);
}
static void get_value(unsigned which){unsigned char *p=range(fnv_cpu_argument_u32(1),4,1);if(!p)return;
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    uint32_t capability=which==0?128:which==1?64:which==2?32:0;
    if(capability && !(o->flags&capability)){finish(CONTROL,8);return;}
    wr(p,which==0?(uint32_t)o->volume:which==1?(uint32_t)o->pan:which==2?o->frequency:(o->playing?1u|(o->looping?4u:0u):0u));finish(0,8);
}
static void volume_get(void){get_value(0);}static void pan_get(void){get_value(1);}static void frequency_get(void){get_value(2);}static void status(void){get_value(3);}
static void lock_buffer(void){
    uint32_t self=fnv_cpu_argument_u32(0),off=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2),a=fnv_cpu_argument_u32(3),b=fnv_cpu_argument_u32(4),c=fnv_cpu_argument_u32(5),d=fnv_cpu_argument_u32(6),flags=fnv_cpu_argument_u32(7);
    unsigned char *p=range(a,4,1),*q=range(b,4,1),*r=c?range(c,4,1):NULL,*s=d?range(d,4,1):NULL;if(!p||!q||(c&&!r)||(d&&!s))return;
    struct sound_object *o=acquire(self,1);if(!o)return;
    if(o->flags&1 || flags>3 || !o->bytes || o->locked){finish(INVALIDCALL,32);return;}
    if(flags&1)off=((uint32_t)(o->phase/48000)+512)*o->channels*(o->bits/8)%o->bytes;
    if(flags&2)bytes=o->bytes;
    if(off>=o->bytes || bytes>o->bytes || !bytes){finish(INVALID,32);return;}
    uint32_t first=o->bytes-off;if(first>bytes)first=bytes;uint32_t second=bytes-first;
    if(!r || !s)second=0;
    o->locked=1;o->offset=off;o->first=first;o->second=second;
    wr(p,o->address+off);wr(q,first);if(r)wr(r,second?o->address:0);if(s)wr(s,second);finish(0,32);
}
static void unlock_buffer(void){
    uint32_t self=fnv_cpu_argument_u32(0),a=fnv_cpu_argument_u32(1),b=fnv_cpu_argument_u32(2),c=fnv_cpu_argument_u32(3),d=fnv_cpu_argument_u32(4);
    /* Validate outside the mixer mutex; callback/unwind cannot hold it. */
    unsigned char *p=b?range(a,b,0):NULL,*q=d?range(c,d,0):NULL;if((b&&!p)||(d&&!q))return;
    struct sound_object *o=acquire(self,1);if(!o)return;
    if(!o->locked || a!=o->address+o->offset || b!=o->first || c!=(o->second?o->address:0) || d!=o->second){finish(INVALID,20);return;}
    if(b)memcpy(o->samples+o->offset,p,b);
    if(d)memcpy(o->samples,q,d);
    o->locked=0;finish(0,20);
}
static void play(void){uint32_t reserved=fnv_cpu_argument_u32(1),priority=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3);
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    if(reserved||priority||flags>1){finish(INVALID,16);return;}
    if((o->flags&1)&&!flags){finish(INVALID,16);return;}
    o->playing=1;o->looping=flags&1;finish(0,16);
}
static void stop(void){struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;o->playing=0;finish(0,4);}
static void seek(void){uint32_t at=fnv_cpu_argument_u32(1);struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    uint32_t block=o->channels*(o->bits/8);if(at>=o->bytes){finish(INVALID,8);return;}o->phase=(uint64_t)(at/block)*48000;finish(0,8);}
static void set_format(void){unsigned char *p=range(fnv_cpu_argument_u32(1),18,0);if(!p)return;uint32_t rate,channels,bits;
    if(!format(p,&rate,&channels,&bits)){recomp_unimpl("DirectSound primary PCM format pending",fnv_cpu_argument_u32(1));return;}
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;
    if(!(o->flags&1)){finish(INVALIDCALL,8);return;}
    if(active->objects[o->parent-1].cooperative<2){finish(0x88780046u,8);return;}
    o->rate=o->frequency=rate;o->channels=channels;o->bits=bits;finish(0,8);
}
static void set_value(unsigned which){int32_t value=(int32_t)fnv_cpu_argument_u32(1);
    struct sound_object *o=acquire(fnv_cpu_argument_u32(0),1);if(!o)return;uint32_t capability=which==0?128:which==1?64:32;
    if(!(o->flags&capability)){finish(CONTROL,8);return;}
    if((which==0&&(value>0||value< -10000))||(which==1&&(value< -10000||value>10000))||(which==2&&value&&(value<100||value>200000))){finish(INVALID,8);return;}
    if(which==2)o->frequency=value?(uint32_t)value:o->rate;
    else {if(!which)o->volume=value;else o->pan=value;
        for(unsigned i=0;i<2;i++){int32_t db=o->volume+(i==0?(o->pan>0?-o->pan:0):(o->pan<0?o->pan:0));
            o->gain[i]=db<= -10000?0:(int32_t)(pow(10.0,db/2000.0)*65536.0+0.5);}}
    finish(0,8);
}
static void volume_set(void){set_value(0);}static void pan_set(void){set_value(1);}static void frequency_set(void){set_value(2);}
static void restore(void){if(!acquire(fnv_cpu_argument_u32(0),1))return;finish(0,4);}
#define PENDING(N) static void pending_##N(void){recomp_unimpl("DirectSound COM method pending",N);}
PENDING(5) PENDING(7) PENDING(9) PENDING(22) PENDING(33) PENDING(34) PENDING(35)
static fnv_cpu_function methods[METHODS]={query,addref,release,create_buffer,device_caps,pending_5,cooperative,pending_7,speakers,pending_9,initialize,certification,
    query,addref,release,buffer_caps,position,get_format,volume_get,pan_get,frequency_get,status,pending_22,lock_buffer,play,seek,set_format,volume_set,pan_set,frequency_set,stop,unlock_buffer,restore,pending_33,pending_34,pending_35};
static const struct fnv_cpu_export exports[]={{"dsound.dll","DirectSoundCreate8",0,0,factory},{"dsound.dll",NULL,11,1,factory}};
const struct fnv_cpu_export *fnv_dsound8_exports(size_t *n){if(n)*n=2;return exports;}
enum fnv_cpu_result fnv_dsound8_create(struct fnv_windows_process *process,const struct fnv_dsound8_config *cfg,struct fnv_dsound8 **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;struct fnv_pcm_stats stats;
    if(!process||!cfg||(cfg->initial_focus!=0&&cfg->initial_focus!=1)||!cfg->output||(cfg->arena_address&3)||cfg->arena_bytes<8192||cfg->arena_bytes>64*1024*1024 || fnv_pcm_stats(cfg->output,&stats)!=FNV_PCM_OK || stats.capacity<512)return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);void *memory;
    enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,cfg->arena_address,cfg->arena_bytes,&memory);if(r)return r;
    if(cfg->memory){unsigned char *data;uint32_t base,bytes;void *backing;
        if(!fnv_vm_binding(cfg->memory,&data,&base,&bytes)||fnv_cpu_prepare_memory(cpu,base,bytes,&backing)!=FNV_CPU_OK||backing!=data||!fnv_vm_access(cfg->memory,cfg->arena_address,cfg->arena_bytes,1))return FNV_CPU_INVALID;}
    struct fnv_dsound8 *d=calloc(1,sizeof(*d));if(!d)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&d->guard)){free(d);return FNV_CPU_NO_MEMORY;}
    d->cpu=cpu;d->config=*cfg;d->memory=memory;d->used=4096;d->focused=(uint32_t)cfg->initial_focus;
    if(cfg->memory&&!fnv_vm_retain(cfg->memory)){fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_INVALID;}
    r=fnv_cpu_register_native(cpu,methods,METHODS,d->tokens);
    if(r){if(cfg->memory)fnv_vm_release_reference(cfg->memory);fnv_host_mutex_destroy(&d->guard);free(d);return r;}
    for(unsigned i=0;i<METHODS;i++)wr(d->memory+i*4,d->tokens[i]);
    active=d;*out=d;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_dsound8_destroy(struct fnv_dsound8 *d){
    if(!d||d!=active)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(d->cpu,d->tokens[0],METHODS);if(r)return r;
    active=NULL;for(unsigned i=0;i<d->count;i++)free(d->objects[i].samples);
    if(d->config.memory)fnv_vm_release_reference(d->config.memory);
    fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_dsound8_set_focus(struct fnv_dsound8 *d,int focused){
    if(!d||d!=active||(focused!=0&&focused!=1))return FNV_CPU_INVALID;
    fnv_host_mutex_lock(&d->guard);d->focused=(uint32_t)focused;fnv_host_mutex_unlock(&d->guard);return FNV_CPU_OK;
}
static int32_t sample(const struct sound_object *o,uint32_t frame,unsigned channel){
    if(o->channels==1)channel=0;
    uint32_t at=(frame*o->channels+channel)*(o->bits/8);
    return o->bits==8?((int32_t)o->samples[at]-128)*256:(int16_t)half(o->samples+at);
}
enum fnv_pcm_result fnv_dsound8_pump(struct fnv_dsound8 *d,uint32_t frames){
    if(!d||d!=active||!frames||frames>512)return FNV_PCM_INVALID;
    int64_t mix[1024]={0};int16_t output[1024];uint64_t phases[CAPACITY];uint32_t playing[CAPACITY];
    fnv_host_mutex_lock(&d->guard);
    for(unsigned i=0;i<d->count;i++){struct sound_object *o=d->objects+i;phases[i]=o->phase;playing[i]=o->playing;
        if(!o->refs||!o->kind||!o->playing||!o->bytes||!d->objects[o->parent-1].cooperative)continue;
        uint32_t count=o->bytes/(o->channels*(o->bits/8));uint64_t end=(uint64_t)count*48000;
        for(unsigned f=0;f<frames&&o->playing;f++){
            uint32_t a=(uint32_t)(o->phase/48000),fraction=(uint32_t)(o->phase%48000),b=a+1;
            if(b==count)b=o->looping?0:a;
            for(unsigned c=0;c<2;c++){int64_t s=(int64_t)sample(o,a,c)*(48000-fraction)+(int64_t)sample(o,b,c)*fraction;
                if(d->focused || (o->flags&0x8000))mix[f*2+c]+=(s/48000)*o->gain[c]/65536;}
            o->phase+=o->frequency;
            if(o->phase>=end){if(o->looping)o->phase%=end;else {o->phase=0;o->playing=0;}}
        }
    }
    for(unsigned i=0;i<frames*2;i++)output[i]=(int16_t)(mix[i]>32767?32767:mix[i]< -32768?-32768:mix[i]);
    enum fnv_pcm_result r=fnv_pcm_write(d->config.output,output,frames);
    if(r!=FNV_PCM_OK)for(unsigned i=0;i<d->count;i++){d->objects[i].phase=phases[i];d->objects[i].playing=playing[i];}
    fnv_host_mutex_unlock(&d->guard);return r;
}
uint32_t fnv_dsound8_produce(void *context,uint32_t frames){return (uint32_t)fnv_dsound8_pump(context,frames);}
