/* Own closed-profile DirectShow pull graph over a real guest IAsyncReader. */
#include "fnv_sdk_runtime.h"
#include "directshow_graph.h"
#include "windows_com_classes.h"
#include "dsound_streams.h"
#include "mp3_stream.h"
#include "guest_memory.h"
#include "host_clock.h"
#include "host_thread.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define GRAPHS 8u
#define IFACES 5u
#define METHODS 27u
#define INVALID 0x80070057u
#define POINTER 0x80004003u
#define NOINTERFACE 0x80004002u
#define ABORTED 0x80004004u
#define NOTIMPL 0x80004001u
#define STATE 0x80040227u
struct graph {
    uint32_t identity,owner,refs,source,reader,scratch,stream,state;
    uint32_t eof,complete,event_pending,rendered;
    int32_t volume,balance;
    uint64_t bytes,offset,duration,origin;
    struct fnv_mp3 *decoder;
};
struct fnv_directshow {
    struct fnv_cpu_runtime *cpu;struct fnv_directshow_config config;
    unsigned char *memory;uint32_t tokens[METHODS],issued;
    fnv_host_mutex guard;
    struct graph graphs[GRAPHS];
};
static struct fnv_directshow *active;
static const unsigned char clsid[16]={0xb3,0xeb,0x36,0xe4,0x4f,0x52,0xce,0x11,0x9f,0x53,0,0x20,0xaf,0x0b,0xa7,0x70};
static const unsigned char async_iid[16]={0xaa,0x68,0xa8,0x56,0xd4,0x0a,0xce,0x11,0xb0,0x3a,0,0x20,0xaf,0x0b,0xa7,0x70};
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(8*i));}
static uint32_t arg(unsigned i){return fnv_cpu_argument_u32(i);}
static void done(uint32_t hr,unsigned bytes){fnv_cpu_return_u32(hr,bytes);}
static void bad(const char *text,uint32_t at){recomp_unimpl(text,at);}
static unsigned char *mem(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,bytes):NULL;
    struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,size;
        if(!fnv_vm_binding(vm,&data,&base,&size) || ((uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base && !fnv_vm_access(vm,at,bytes,write)))p=NULL;}
    if(!p)bad("DirectShow inaccessible guest memory",at);return p;
}
static uint32_t thread_id(void){uint32_t lo,hi,id;fnv_windows_current_stack_bounds(&lo,&hi,&id);return id;}
static uint32_t invoke(uint32_t object,unsigned slot,const uint32_t *args,unsigned count){
    if(count>6){bad("DirectShow callback argument budget",count);return 0;}
    unsigned char *p=mem(object,4,0);if(!p)return 0;uint32_t table=rd(p);
    p=mem(table+slot*4,4,0);if(!p)return 0;uint32_t target=rd(p);
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || state.gpr[4]>high || state.gpr[4]<low || state.gpr[4]-low<256 || (state.gpr[4]&3)){
        bad("DirectShow callback stack unavailable",target);return 0;}
    uint32_t frame=state.gpr[4]-256;p=mem(frame,8+count*4,1);unsigned char *ret=mem(state.gpr[4],4,0);if(!p || !ret)return 0;
    wr(p,rd(ret));wr(p+4,object);for(unsigned i=0;i<count;i++)wr(p+8+i*4,args[i]);
    return fnv_cpu_invoke_stdcall(target,frame,4+count*4);
}
static int iid_kind(const unsigned char *p){
    static const unsigned char tail[12]={0xd4,0x0a,0xce,0x11,0xb0,0x3a,0,0x20,0xaf,0x0b,0xa7,0x70};
    static const unsigned char unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    if(!memcmp(p,unknown,16))return 0;
    if(memcmp(p+4,tail,12))return -1;
    switch(rd(p)){case 0x56a8689f:case 0x56a868a9:return 0;case 0x56a868b3:return 1;case 0x56a868b1:return 2;case 0x56a868b2:return 3;case 0x56a868b6:return 4;default:return -1;}
}
static struct graph *self(void){
    if(!active || active->cpu!=fnv_cpu_current()){bad("DirectShow provider unavailable",0);return NULL;}
    uint32_t at=arg(0),id=thread_id();
    fnv_host_mutex_lock(&active->guard);
    for(unsigned i=0;i<GRAPHS;i++){struct graph *g=active->graphs+i;
        if(g->refs && g->refs!=UINT32_MAX && at>=g->identity && at-g->identity<IFACES*4 && !((at-g->identity)&3)){
            fnv_host_mutex_unlock(&active->guard);
            if(g->owner!=id){bad("DirectShow cross-thread graph access unsupported",at);return NULL;}return g;
        }}
    fnv_host_mutex_unlock(&active->guard);bad("DirectShow stale interface",at);return NULL;
}
static uint32_t control(struct graph *g){return fnv_dsound_stream_control(active->config.sound,g->stream,g->state!=2,g->volume,g->balance)==FNV_PCM_OK?0:STATE;}
static uint64_t played(struct graph *g){struct fnv_dsound_stream_stats s;
    if(fnv_dsound_stream_stats(active->config.sound,g->stream,&s)!=FNV_PCM_OK){bad("DirectShow music stream unavailable",g->stream);return 0;}return g->origin+s.played;
}
static int source_read(void *context,void *output,size_t capacity,size_t *read){
    struct graph *g=context;*read=0;if(g->offset>g->bytes)return 0;
    if(capacity>g->bytes-g->offset)capacity=(size_t)(g->bytes-g->offset);
    while(*read<capacity){uint32_t n=(uint32_t)(capacity-*read);if(n>32768)n=32768;
        uint32_t args[]={(uint32_t)g->offset,(uint32_t)(g->offset>>32),n,g->scratch+4096};
        uint32_t hr=invoke(g->reader,7,args,4);if(hr)return 0;
        unsigned char *p=mem(g->scratch+4096,n,0);if(!p)return 0;memcpy((unsigned char *)output+*read,p,n);g->offset+=n;*read+=n;
    }return 1;
}
static int source_seek(void *context,uint64_t position){struct graph *g=context;if(position>g->bytes)return 0;g->offset=position;return 1;}
static uint32_t fill(struct graph *g){
    if(!g->rendered)return STATE;
    if(g->state!=2)return 0;
    struct fnv_dsound_stream_stats s;
    if(fnv_dsound_stream_stats(active->config.sound,g->stream,&s)!=FNV_PCM_OK)return STATE;
    int16_t pcm[1024];
    for(uint32_t available=s.capacity-s.queued;!g->eof && available;){
        uint32_t count=available>512?512:available,n=0;enum fnv_mp3_result r=fnv_mp3_read(g->decoder,pcm,count,&n);
        if(r!=FNV_MP3_OK && r!=FNV_MP3_EOF)return 0x80040218u;
        if(n && fnv_dsound_stream_write(active->config.sound,g->stream,pcm,n)!=FNV_PCM_OK)return STATE;
        available-=n;if(r==FNV_MP3_EOF)g->eof=1;else if(!n)return 0x80040218u;
    }
    if(fnv_dsound_stream_stats(active->config.sound,g->stream,&s)!=FNV_PCM_OK)return STATE;
    if(g->eof && !s.queued && !g->complete){g->complete=g->event_pending=1;}
    return 0;
}
static uint32_t seek_frame(struct graph *g,uint64_t frame){
    if(!g->rendered || frame>g->duration)return INVALID;
    if(fnv_dsound_stream_control(active->config.sound,g->stream,1,g->volume,g->balance)!=FNV_PCM_OK || fnv_dsound_stream_reset(active->config.sound,g->stream)!=FNV_PCM_OK)return STATE;
    if(fnv_mp3_rewind(g->decoder)!=FNV_MP3_OK)return 0x80040218u;
    int16_t pcm[1024];uint64_t skipped=0;
    while(skipped<frame){uint32_t n=0,want=frame-skipped>512?512:(uint32_t)(frame-skipped);enum fnv_mp3_result r=fnv_mp3_read(g->decoder,pcm,want,&n);
        if((r!=FNV_MP3_OK && r!=FNV_MP3_EOF) || !n)return 0x80040218u;skipped+=n;}
    g->origin=frame;g->eof=g->complete=g->event_pending=0;uint32_t hr=fill(g);return hr?hr:control(g);
}
static void query(void){struct graph *g=self();if(!g)return;
    unsigned char *iid=mem(arg(1),16,0),*out=mem(arg(2),4,1);if(!iid || !out)return;
    int kind=iid_kind(iid);wr(out,0);if(kind<0){done(NOINTERFACE,12);return;}
    if(g->refs>=UINT32_MAX-1){bad("DirectShow reference overflow",arg(0));return;}fnv_host_mutex_lock(&active->guard);g->refs++;fnv_host_mutex_unlock(&active->guard);wr(out,g->identity+(uint32_t)kind*4);done(0,12);
}
static void addref(void){struct graph *g=self();if(!g)return;if(g->refs>=UINT32_MAX-1){bad("DirectShow reference overflow",arg(0));return;}fnv_host_mutex_lock(&active->guard);uint32_t refs=++g->refs;fnv_host_mutex_unlock(&active->guard);done(refs,4);}
static void release(void){struct graph *g=self();if(!g)return;
    fnv_host_mutex_lock(&active->guard);uint32_t refs=--g->refs;if(!refs)g->refs=UINT32_MAX;fnv_host_mutex_unlock(&active->guard);
    if(refs){done(refs,4);return;}
    if(g->decoder){fnv_mp3_close(g->decoder);g->decoder=NULL;}
    if(g->stream)(void)fnv_dsound_stream_close(active->config.sound,g->stream);
    if(g->reader)invoke(g->reader,2,NULL,0);
    if(g->source){uint32_t args[]={0,0};invoke(g->source,13,args,2);invoke(g->source,2,NULL,0);}
    fnv_host_mutex_lock(&active->guard);memset(g,0,sizeof(*g));fnv_host_mutex_unlock(&active->guard);done(0,4);
}
static void unsupported(void){bad("DirectShow method outside pull-MP3 profile",arg(0));}
static void type_count(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),4,1);if(!p)return;wr(p,0);done(0,8);}
static void add_filter(void){struct graph *g=self();if(!g)return;uint32_t source=arg(1),name=arg(2);
    if(!source){done(POINTER,12);return;}if(g->source){done(STATE,12);return;}
    if(!name){unsigned char *n=mem(g->scratch+48,16,1);if(!n)return;static const unsigned char generated[16]={'S',0,'o',0,'u',0,'r',0,'c',0,'e',0,0,0,0,0};memcpy(n,generated,16);name=g->scratch+48;}
    invoke(source,1,NULL,0);uint32_t args[]={g->identity,name};uint32_t hr=invoke(source,13,args,2);
    if((int32_t)hr<0)invoke(source,2,NULL,0);else g->source=source;done(hr,12);
}
static void render(void){struct graph *g=self();if(!g)return;uint32_t pin=arg(1);
    if(!g->source || g->rendered){done(STATE,8);return;}
    unsigned char *p=mem(g->scratch,48,1);if(!p)return;memcpy(p,async_iid,16);wr(p+16,0);
    uint32_t qa[]={g->scratch,g->scratch+16};uint32_t hr=invoke(pin,0,qa,2);
    if((int32_t)hr<0 || !(g->reader=rd(p+16))){done((int32_t)hr<0?hr:NOINTERFACE,8);return;}
    uint32_t la[]={g->scratch+24,g->scratch+32};hr=invoke(g->reader,8,la,2);
    uint64_t bytes=0,available=0;memcpy(&bytes,p+24,8);memcpy(&available,p+32,8);
    if(hr || !bytes || bytes>128u*1024*1024 || available<bytes){invoke(g->reader,2,NULL,0);g->reader=0;done(hr?hr:0x80040218u,8);return;}
    g->bytes=bytes;g->offset=0;struct fnv_mp3_io io={g,source_read,source_seek};
    enum fnv_mp3_result r=fnv_mp3_open(&io,&g->decoder);
    if(r==FNV_MP3_OK){int16_t pcm[1024];uint32_t n;uint64_t frames=0;
        do {n=0;r=fnv_mp3_read(g->decoder,pcm,512,&n);frames+=n;if(frames>48000ull*14400){r=FNV_MP3_FORMAT;break;}}while(r==FNV_MP3_OK);
        if(r==FNV_MP3_EOF && frames && fnv_mp3_rewind(g->decoder)==FNV_MP3_OK){g->duration=frames;r=FNV_MP3_OK;}else if(r==FNV_MP3_EOF)r=FNV_MP3_FORMAT;
    }
    if(r!=FNV_MP3_OK){fnv_mp3_close(g->decoder);g->decoder=NULL;invoke(g->reader,2,NULL,0);g->reader=0;done(0x80040218u,8);return;}
    if(fnv_dsound_stream_open(active->config.sound,&g->stream)!=FNV_PCM_OK){fnv_mp3_close(g->decoder);g->decoder=NULL;invoke(g->reader,2,NULL,0);g->reader=0;done(0x8007000eu,8);return;}
    g->rendered=1;done(0,8);
}
static void transition(unsigned state){struct graph *g=self();if(!g)return;if(!g->rendered){done(STATE,4);return;}
    uint32_t old=g->state;g->state=state;uint32_t hr=control(g);
    if(!hr){uint32_t zero[]={0,0};hr=invoke(g->source,state==2?6:state==1?5:4,zero,state==2?2:0);}
    if((int32_t)hr<0){g->state=old;(void)control(g);done(hr,4);return;}
    if(state==0)hr=seek_frame(g,0);else if(state==2)hr=fill(g);done(hr,4);
}
static void run(void){transition(2);}static void pause_graph(void){transition(1);}static void stop(void){transition(0);}
static void get_state(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(2),4,1);if(!p)return;wr(p,g->state);done(0,12);}
static void volume(int get,int balance){struct graph *g=self();if(!g)return;
    int32_t *value=balance?&g->balance:&g->volume;uint32_t hr=0;
    if(get){unsigned char *p=mem(arg(1),4,1);if(!p)return;wr(p,(uint32_t)*value);}
    else {int32_t v=(int32_t)arg(1);if(v< -10000 || v>(balance?10000:0))hr=INVALID;else {int32_t old=*value;*value=v;if(g->rendered && (hr=control(g)))*value=old;}}
    done(hr,8);
}
static void put_volume(void){volume(0,0);}static void get_volume(void){volume(1,0);}static void put_balance(void){volume(0,1);}static void get_balance(void){volume(1,1);}
static void position(int duration){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),8,1);if(!p)return;
    if(!g->rendered){done(STATE,8);return;}uint32_t hr=fill(g);double seconds=(double)(duration?g->duration:played(g))/48000.0;memcpy(p,&seconds,8);done(hr,8);
}
static void get_duration(void){position(1);}static void get_position(void){position(0);}
static void set_position(void){struct graph *g=self();if(!g)return;uint64_t bits=arg(1)|(uint64_t)arg(2)<<32;double v;memcpy(&v,&bits,8);
    if(!isfinite(v) || v<0 || v>(double)g->duration/48000.0){done(INVALID,12);return;}done(seek_frame(g,(uint64_t)llround(v*48000.0)),12);
}
static void get_rate(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),8,1);if(!p)return;double v=1.0;memcpy(p,&v,8);done(0,8);}
static void put_rate(void){struct graph *g=self();if(!g)return;uint64_t bits=arg(1)|(uint64_t)arg(2)<<32;double v;memcpy(&v,&bits,8);done(v==1.0?0:INVALID,12);}
static void can_seek(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),4,1);if(!p)return;wr(p,g->rendered?UINT32_MAX:0);done(0,8);}
static void preroll(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),8,1);if(!p)return;memset(p,0,8);done(0,8);}
static void get_event(void){struct graph *g=self();if(!g)return;
    unsigned char *code=mem(arg(1),4,1),*a=mem(arg(2),4,1),*b=mem(arg(3),4,1);if(!code || !a || !b)return;
    uint32_t hr=fill(g);if(hr){done(hr,20);return;}if(arg(4)){bad("DirectShow blocking GetEvent unsupported",arg(4));return;}
    if(g->event_pending){wr(code,1);wr(a,0);wr(b,0);g->event_pending=0;done(0,20);}else done(ABORTED,20);
}
static void wait_completion(void){struct graph *g=self();if(!g)return;unsigned char *code=mem(arg(2),4,1);if(!code)return;
    int32_t timeout=(int32_t)arg(1);uint64_t start,now;if(timeout< -1 || !fnv_host_now_ns(&start)){done(INVALID,12);return;}
    for(;;){uint32_t hr=fill(g);if(hr){done(hr,12);return;}if(g->complete){wr(code,1);done(0,12);return;}
        if(!fnv_host_now_ns(&now) || now<start){bad("DirectShow clock unavailable",0);return;}
        if(timeout>=0 && (now-start)/1000000>=(uint32_t)timeout){done(ABORTED,12);return;}
        if(fnv_cpu_is_faulted(active->cpu))fnv_cpu_stop_for_peer();fnv_host_thread_sleep(1);
    }
}
static void free_event(void){struct graph *g=self();if(!g)return;done(arg(1)==1 && !arg(2) && !arg(3)?0:INVALID,16);}
static void notimpl_handle(void){struct graph *g=self();if(!g)return;unsigned char *p=mem(arg(1),4,1);if(!p)return;wr(p,0);done(NOTIMPL,8);}
static void cancel_event(void){struct graph *g=self();if(!g)return;done(INVALID,8);}
static const fnv_cpu_function methods[METHODS]={query,addref,release,unsupported,type_count,add_filter,render,run,pause_graph,stop,get_state,put_volume,get_volume,put_balance,get_balance,get_duration,set_position,get_position,get_rate,put_rate,can_seek,preroll,get_event,wait_completion,free_event,notimpl_handle,cancel_event};
static int factory(void *context,const unsigned char *c,uint32_t outer,uint32_t flags,const unsigned char *iid,uint32_t *hr,uint32_t *out){
    struct fnv_directshow *d=context;if(memcmp(c,clsid,16))return 0;
    *out=0;if(d!=active || d->cpu!=fnv_cpu_current()){bad("DirectShow factory runtime unavailable",0);return 1;}
    if(outer){*hr=0x80040110u;return 1;}if(flags!=1){*hr=0x80040154u;return 1;}
    int kind=iid_kind(iid);if(kind<0){*hr=NOINTERFACE;return 1;}
    uint32_t owner=thread_id();fnv_host_mutex_lock(&d->guard);
    if(d->issued==512){fnv_host_mutex_unlock(&d->guard);*hr=0x8007000eu;return 1;}
    for(unsigned i=0;i<GRAPHS;i++)if(!d->graphs[i].refs){struct graph *g=d->graphs+i;
        memset(g,0,sizeof(*g));g->identity=d->config.arena_address+0x4000+d->issued++*32;g->owner=owner;g->refs=1;g->scratch=d->config.arena_address+0x10000+i*0x10000;
        for(unsigned j=0;j<IFACES;j++)wr(d->memory+g->identity-d->config.arena_address+j*4,d->config.arena_address+j*256);
        *out=g->identity+(uint32_t)kind*4;*hr=0;fnv_host_mutex_unlock(&d->guard);return 1;
    }
    fnv_host_mutex_unlock(&d->guard);*hr=0x8007000eu;return 1;
}
enum fnv_cpu_result fnv_directshow_create(struct fnv_windows_process *p,const struct fnv_directshow_config *c,struct fnv_directshow **out){
    if(!out)return FNV_CPU_INVALID;*out=NULL;
    if(!p || !c || !c->sound || (c->arena_address&4095) || c->arena_bytes<0x90000 || active)return FNV_CPU_INVALID;
    void *memory;struct fnv_cpu_runtime *cpu=fnv_windows_runtime(p);enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,c->arena_address,c->arena_bytes,&memory);if(r)return r;
    if(c->memory){unsigned char *data;uint32_t base,bytes;void *backing;
        if(!fnv_vm_binding(c->memory,&data,&base,&bytes) || fnv_cpu_prepare_memory(cpu,base,bytes,&backing)!=FNV_CPU_OK || backing!=data || !fnv_vm_access(c->memory,c->arena_address,c->arena_bytes,1))return FNV_CPU_INVALID;}
    struct fnv_directshow *d=calloc(1,sizeof(*d));if(!d)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&d->guard)){free(d);return FNV_CPU_NO_MEMORY;}
    d->cpu=cpu;d->config=*c;d->memory=memory;
    /* Register only actual functions; unused method table slots map to the
     * explicit unsupported boundary, never a successful generic stub. */
    r=fnv_cpu_register_native(cpu,methods,METHODS,d->tokens);if(r){fnv_host_mutex_destroy(&d->guard);free(d);return r;}
    for(unsigned k=0;k<IFACES;k++)for(unsigned j=0;j<64;j++)wr(d->memory+k*256+j*4,d->tokens[j<3?j:3]);
    unsigned map[IFACES][18]={{0,1,2,5,3,3,3,3,3,3,3,3,6,3,3,3,3,3},{0,1,2,4,3,3,3,11,12,13,14,3,3,3,3,3,3,3},{0,1,2,4,3,3,3,7,8,9,10,3,3,3,3,9,3,3},{0,1,2,4,3,3,3,15,16,17,15,3,21,3,19,18,20,20},{0,1,2,4,3,3,3,25,22,23,26,26,24,3,3,3,3,3}};
    for(unsigned k=0;k<IFACES;k++)for(unsigned j=0;j<18;j++)wr(d->memory+k*256+j*4,d->tokens[map[k][j]]);
    if(c->memory && !fnv_vm_retain(c->memory)){fnv_cpu_unregister_native(cpu,d->tokens[0],METHODS);fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_INVALID;}
    active=d;if(!fnv_windows_com_set_factory(d,factory)){active=NULL;if(c->memory)fnv_vm_release_reference(c->memory);fnv_cpu_unregister_native(cpu,d->tokens[0],METHODS);fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_BUSY;}
    *out=d;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_directshow_destroy(struct fnv_directshow *d){
    if(!d || active!=d)return FNV_CPU_INVALID;for(unsigned i=0;i<GRAPHS;i++)if(d->graphs[i].refs)return FNV_CPU_BUSY;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(d->cpu,d->tokens[0],METHODS);if(r)return r;
    fnv_windows_com_set_factory(NULL,NULL);active=NULL;if(d->config.memory)fnv_vm_release_reference(d->config.memory);fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_OK;
}
