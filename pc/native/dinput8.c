/* Original bounded x86 DirectInput8A COM bridge. No Windows DLL code. */
#include "fnv_sdk_runtime.h"
#include "dinput8.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include "host_wallclock.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define DI_INVALID 0x80070057u
#define DI_NOINTERFACE 0x80004002u
#define DI_NOREG 0x80040154u
#define DI_NOTACQUIRED 0x8007000cu
#define DI_NOTINITIALIZED 0x80070015u
#define DI_ACQUIRED 0x800700aau
#define DI_METHODS 43u
#define DI_CAPACITY 64u
static const unsigned char iid_root[16]={0x30,0x80,0x79,0xbf,0x3a,0x48,0xa2,0x4d,0xaa,0x99,0x5d,0x64,0xed,0x36,0x97,0};
static const unsigned char iid_device[16]={0x80,0x10,0xd4,0x54,0x15,0xdc,0x33,0x48,0xa4,0x1b,0x74,0x8f,0x73,0xa3,0x81,0x79};
static const unsigned char iid_unknown[16]={0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
struct di_event {uint32_t offset,data,timestamp,sequence;};
struct di_object {uint32_t refs,kind,parent,format,cooperative,acquired,window,lost,requested,capacity,head,queued,overflow;struct di_event *events;int64_t last[3];};
struct fnv_dinput8 {struct fnv_cpu_runtime *cpu;struct fnv_dinput8_config config;fnv_host_mutex guard;
    unsigned char *memory;uint32_t tokens[DI_METHODS],count,sequence;struct di_object objects[DI_CAPACITY];
    struct fnv_di_snapshot snapshot;int64_t totals[3];};
static struct fnv_dinput8 *active;
static uint32_t rd(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static unsigned char *range(uint32_t at,uint32_t n,int write){
    unsigned char *p=at?fnv_cpu_try_memory(at,n):NULL;struct fnv_guest_memory *vm=fnv_windows_current_memory();
    if(vm){unsigned char *data;uint32_t base,bytes;if(!fnv_vm_binding(vm,&data,&base,&bytes) ||
        ((uint64_t)at<(uint64_t)base+bytes && (uint64_t)at+n>base && !fnv_vm_access(vm,at,n,write)))p=NULL;}
    if(!p)recomp_unimpl("DirectInput inaccessible guest buffer",at);
    return p;
}
static int current(void){if(active && active->cpu==fnv_cpu_current())return 1;recomp_unimpl("DirectInput provider unavailable",0);return 0;}
static uint32_t address(unsigned i){return active->config.objects_address+512+i*8;}
static int index_of(uint32_t at){
    if(at<active->config.objects_address+512 || (at-active->config.objects_address-512)%8)return -1;
    uint32_t i=(at-active->config.objects_address-512)/8;
    return i<active->count && active->objects[i].refs?(int)i:-1;
}
static int kind_guid(const unsigned char *g){
    static const unsigned char tail[12]={0xa0,0xd5,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
    if(memcmp(g+4,tail,12))return 0;
    uint32_t id=rd(g);return id==0x6f1d2b61?1:id==0x6f1d2b60?2:0;
}
static uint32_t allocate_locked(uint32_t kind,uint32_t parent){
    if(active->count==DI_CAPACITY)return 0;
    unsigned i=active->count++;struct di_object *o=active->objects+i;
    *o=(struct di_object){0};o->refs=1;o->kind=kind;o->parent=parent;
    if(parent)active->objects[parent-1].refs++;
    wr(active->memory+512+i*8,active->config.objects_address+(kind?44:0));return address(i);
}
static void api_factory(void){
    uint32_t instance=fnv_cpu_argument_u32(0),version=fnv_cpu_argument_u32(1),iid=fnv_cpu_argument_u32(2),out=fnv_cpu_argument_u32(3),outer=fnv_cpu_argument_u32(4);
    if(!current())return;
    unsigned char *p=range(out,4,1),*guid=range(iid,16,0);if(!p || !guid)return;
    unsigned char identity[16];memcpy(identity,guid,16);
    wr(p,0);uint32_t hr=0,value=0;
    if(outer){recomp_unimpl("DirectInput aggregation pending",outer);return;}
    if(instance!=active->config.instance)hr=DI_INVALID;
    else if(version!=0x800){recomp_unimpl("DirectInput version pending",version);return;}
    else if(memcmp(identity,iid_root,16))hr=DI_NOINTERFACE;
    else {fnv_host_mutex_lock(&active->guard);value=allocate_locked(0,0);fnv_host_mutex_unlock(&active->guard);if(!value)hr=0x8007000e;}
    wr(p,value);fnv_cpu_return_u32(hr,20);
}
static void api_query(void){
    uint32_t self=fnv_cpu_argument_u32(0),iid=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2);if(!current())return;
    unsigned char *p=range(out,4,1),*g=range(iid,16,0);if(!p || !g)return;
    unsigned char guid[16];memcpy(guid,g,16);wr(p,0);uint32_t hr=DI_NOINTERFACE;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i>=0 && (!memcmp(guid,iid_unknown,16) || !memcmp(guid,active->objects[i].kind?iid_device:iid_root,16))){
        if(active->objects[i].refs<UINT32_MAX){active->objects[i].refs++;wr(p,self);hr=0;}else hr=0x8007000e;}
    fnv_host_mutex_unlock(&active->guard);
    if(i<0){recomp_unimpl("DirectInput stale COM identity",self);return;}fnv_cpu_return_u32(hr,12);
}
static void ref(int add){
    uint32_t self=fnv_cpu_argument_u32(0),value=0;if(!current())return;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i>=0){struct di_object *o=active->objects+i;
        if(add){if(o->refs!=UINT32_MAX)value=++o->refs;}else {value=--o->refs;if(!value){free(o->events);o->events=NULL;if(o->parent)active->objects[o->parent-1].refs--;}}}
    fnv_host_mutex_unlock(&active->guard);
    if(i<0 || (add && !value)){recomp_unimpl("DirectInput COM reference invalid",self);return;}fnv_cpu_return_u32(value,4);
}
static void api_addref(void){ref(1);}static void api_release(void){ref(0);}
static void api_create_device(void){
    uint32_t self=fnv_cpu_argument_u32(0),guid=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2),outer=fnv_cpu_argument_u32(3);if(!current())return;
    unsigned char *g=range(guid,16,0),*p=range(out,4,1);if(!g || !p)return;
    int kind=kind_guid(g);wr(p,0);if(outer){recomp_unimpl("DirectInput device aggregation pending",outer);return;}
    uint32_t hr=0,value=0;fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i<0 || active->objects[i].kind)hr=DI_INVALID;
    else if(!kind || (kind==1?!active->config.keyboard:!active->config.mouse))hr=DI_NOREG;
    else if(active->objects[i].refs==UINT32_MAX)hr=0x8007000e;
    else {value=allocate_locked((uint32_t)kind,(uint32_t)i+1);if(!value)hr=0x8007000e;}
    fnv_host_mutex_unlock(&active->guard);wr(p,value);fnv_cpu_return_u32(hr,16);
}
static void api_enum(void){
    uint32_t self=fnv_cpu_argument_u32(0),type=fnv_cpu_argument_u32(1),callback=fnv_cpu_argument_u32(2),context=fnv_cpu_argument_u32(3),flags=fnv_cpu_argument_u32(4);
    if(!current())return;
    if(flags&~1u || (type>4 && type!=18 && type!=19)){recomp_unimpl("DirectInput enumeration filter pending",type);return;}
    struct fnv_cpu_state state;uint32_t low,high,id;fnv_windows_current_stack_bounds(&low,&high,&id);(void)id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK || (state.gpr[4]&3) || state.gpr[4]>high || state.gpr[4]<low || state.gpr[4]-low<768){
        recomp_unimpl("DirectInput enumeration stack unavailable",self);return;}
    unsigned char *frame=range(state.gpr[4]-768,12,1),*desc=range(state.gpr[4]-720,580,1);if(!frame || !desc)return;
    if(!callback || !RECOMP_ICALL_IS_CODE(callback) || (!recomp_lookup(callback) && !recomp_lookup_kernel(callback))){recomp_unimpl("DirectInput enumeration callback pending",callback);return;}
    fnv_host_mutex_lock(&active->guard);int idx=index_of(self);
    int valid=idx>=0 && !active->objects[idx].kind && active->objects[idx].refs<UINT32_MAX;
    if(valid)active->objects[idx].refs++;
    fnv_host_mutex_unlock(&active->guard);
    if(!valid){fnv_cpu_return_u32(DI_INVALID,20);return;}
    for(unsigned kind=1;kind<=2;kind++){
        if((kind==1?!active->config.keyboard:!active->config.mouse) || type==4 ||
           (type==2 && kind!=2) || (type==3 && kind!=1) || (type==18 && kind!=2) || (type==19 && kind!=1))continue;
        memset(desc,0,580);wr(desc,580);
        static const unsigned char tail[]={0xa0,0xd5,0xcf,0x11,0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
        wr(desc+4,kind==1?0x6f1d2b61:0x6f1d2b60);memcpy(desc+8,tail,12);memcpy(desc+20,desc+4,16);wr(desc+36,kind==1?19:18);
        strcpy((char *)desc+40,kind==1?"FNV virtual keyboard":"FNV virtual mouse");strcpy((char *)desc+300,"Switch input bridge");
        desc[576]=1;desc[578]=kind==1?6:2;
        wr(frame,rd(range(state.gpr[4],4,0)));wr(frame+4,state.gpr[4]-720);wr(frame+8,context);
        if(!fnv_cpu_invoke_stdcall(callback,state.gpr[4]-768,8))break;
    }
    fnv_host_mutex_lock(&active->guard);int retained=active->objects[idx].refs!=0;
    if(retained)active->objects[idx].refs--;
    fnv_host_mutex_unlock(&active->guard);
    if(!retained){recomp_unimpl("DirectInput callback released retained root",self);return;}
    fnv_cpu_return_u32(0,20);
}
static void api_status(void){
    uint32_t self=fnv_cpu_argument_u32(0),guid=fnv_cpu_argument_u32(1);if(!current())return;
    unsigned char *g=range(guid,16,0);if(!g)return;int kind=kind_guid(g);uint32_t hr=DI_NOREG;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i>=0 && !active->objects[i].kind && ((kind==1 && active->config.keyboard)||(kind==2 && active->config.mouse)))hr=0;
    fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,8);
}
static int focused(uint32_t hwnd){
    if(!active->config.has_input_focus){recomp_unimpl("DirectInput foreground focus tracking pending",hwnd);return 0;}
    int value=active->config.has_input_focus(active->config.context,hwnd);
    if(value!=0 && value!=1){recomp_unimpl("DirectInput foreground focus unavailable",hwnd);return 0;}
    return value;
}
static void acquire(int on){
    uint32_t self=fnv_cpu_argument_u32(0),hr=0;if(!current())return;
    struct di_object before={0};fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {before=active->objects[i];if(on && (!before.format || !before.cooperative))hr=DI_NOTINITIALIZED;}
    fnv_host_mutex_unlock(&active->guard);
    if(hr){fnv_cpu_return_u32(hr,4);return;}
    int focus=on && (before.cooperative&4)?focused(before.window):1;
    fnv_host_mutex_lock(&active->guard);i=index_of(self);
    if(i<0 || active->objects[i].cooperative!=before.cooperative || active->objects[i].window!=before.window || active->objects[i].format!=before.format)hr=DI_INVALID;
    else {struct di_object *o=active->objects+i;
        if(on && !focus){if(o->acquired)o->lost=1;o->acquired=0;hr=0x80070005u;}
        else {hr=o->acquired==!!on?1:0;
            if(on && !o->acquired)memcpy(o->last,active->totals,sizeof(o->last));
            o->acquired=!!on;o->lost=0;if(!on || !hr){o->head=o->queued=o->overflow=0;}}}
    fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,4);
}
static void api_acquire(void){acquire(1);}static void api_unacquire(void){acquire(0);}
static void api_cooperative(void){
    uint32_t self=fnv_cpu_argument_u32(0),window=fnv_cpu_argument_u32(1),flags=fnv_cpu_argument_u32(2);if(!current())return;
    if((flags&~31u) || ((flags&3)!=1 && (flags&3)!=2) || ((flags&12)!=4 && (flags&12)!=8)){
        fnv_cpu_return_u32(DI_INVALID,12);return;}
    if(!active->config.valid_window){recomp_unimpl("DirectInput window validator unavailable",window);return;}
    int valid=active->config.valid_window(active->config.context,window);uint32_t hr=valid?0:0x80070006;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else if(active->objects[i].acquired)hr=DI_ACQUIRED;
    else if(!hr){active->objects[i].cooperative=flags;active->objects[i].window=window;}
    fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,12);
}
static void api_format(void){
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1);if(!current())return;
    unsigned char *p=range(at,24,0);if(!p)return;uint32_t header[6];for(unsigned i=0;i<6;i++)header[i]=rd(p+i*4);
    fnv_host_mutex_lock(&active->guard);int idx=index_of(self);uint32_t kind=idx>=0?active->objects[idx].kind:0;fnv_host_mutex_unlock(&active->guard);
    uint32_t count=header[4],bytes=header[3],hr=0;
    if(header[0]!=24 || header[1]!=16 || header[2]!=2 || !kind ||
       (kind==1?(bytes!=256 || count!=256):((bytes!=16 && bytes!=20) || count!=bytes-9))){fnv_cpu_return_u32(DI_INVALID,8);return;}
    unsigned char *objects=range(header[5],count*16,0);if(!objects)return;
    for(uint32_t i=0;i<count;i++){
        unsigned char *item=objects+i*16;uint32_t ofs=rd(item+4),type=rd(item+8),fl=rd(item+12);
        uint32_t expected=kind==1?i:i<3?i*4:i+9;
        unsigned char *guid=rd(item)?range(rd(item),16,0):NULL;
        /* Standard keyboard/mouse layouts only; never retain guest pointers. */
        uint32_t low=type&255u,instance=(type>>8)&65535u;
        if(ofs!=expected || (type&0x7f000000u) || (instance!=(kind==1 || i<3?i:i-3) && instance!=65535u) ||
           (kind==1?(low!=12 && low!=4):(i<3?low!=3 && low!=1:low!=12 && low!=4)) || (fl && fl!=0x100u))hr=DI_INVALID;
        if(guid){uint32_t ident=rd(guid);
            static const unsigned char suffix[8]={0xbf,0xc7,0x44,0x45,0x53,0x54,0,0};
            if((kind==1?ident!=0x55728220u:i<3?ident!=0xa36d02e0u+i:ident!=0xa36d02f0u) ||
               rd(guid+4)!=(kind==1?0x11cfd33cu:0x11cfc9f3u) || memcmp(guid+8,suffix,8))hr=DI_INVALID;}
    }
    fnv_host_mutex_lock(&active->guard);idx=index_of(self);
    if(idx<0)hr=DI_INVALID;else if(active->objects[idx].acquired)hr=DI_ACQUIRED;
    else if(!hr)active->objects[idx].format=bytes;
    fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,8);
}
static int32_t delta(int64_t now,int64_t last){
    if(now>=last){uint64_t v=(uint64_t)now-(uint64_t)last;return v>INT32_MAX?INT32_MAX:(int32_t)v;}
    uint64_t v=(uint64_t)last-(uint64_t)now;return v>=UINT64_C(2147483648)?INT32_MIN:-(int32_t)v;
}
static void api_state(void){
    if(!current())return;
    if(active->config.service_text)active->config.service_text(active->config.context);
    uint32_t self=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2);if(!current())return;
    unsigned char *p=range(out,bytes,1);if(!p)return;
    struct di_object before={0};fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i>=0)before=active->objects[i];
    fnv_host_mutex_unlock(&active->guard);
    int focus=before.acquired && (before.cooperative&4)?focused(before.window):1;
    unsigned char data[256]={0};uint32_t hr=0;fnv_host_mutex_lock(&active->guard);i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {struct di_object *o=active->objects+i;
        if(o->cooperative!=before.cooperative || o->window!=before.window || o->format!=before.format)hr=DI_INVALID;
        else {
            if(o->acquired && !focus){o->acquired=0;o->lost=1;o->head=o->queued=o->overflow=0;}
            if(!o->acquired)hr=o->lost?0x8007001eu:DI_NOTACQUIRED;
            else if(bytes!=o->format)hr=DI_INVALID;
            else if(o->kind==1)memcpy(data,active->snapshot.keys,256);
            else {for(unsigned j=0;j<3;j++)wr(data+j*4,(uint32_t)delta(active->totals[j],o->last[j]));
                memcpy(data+12,active->snapshot.buttons,bytes-12);memcpy(o->last,active->totals,sizeof(o->last));}}}
    fnv_host_mutex_unlock(&active->guard);if(!hr)memcpy(p,data,bytes);fnv_cpu_return_u32(hr,12);
}
static void api_poll(void){
    uint32_t self=fnv_cpu_argument_u32(0);if(!current())return;
    struct di_object before={0};fnv_host_mutex_lock(&active->guard);int i=index_of(self);if(i>=0)before=active->objects[i];fnv_host_mutex_unlock(&active->guard);
    int focus=before.acquired && (before.cooperative&4)?focused(before.window):1;
    uint32_t hr;fnv_host_mutex_lock(&active->guard);i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {struct di_object *o=active->objects+i;
        if(o->cooperative!=before.cooperative || o->window!=before.window || o->format!=before.format)hr=DI_INVALID;
        else {if(o->acquired && !focus){o->acquired=0;o->lost=1;o->head=o->queued=o->overflow=0;}
            /* These interrupt/snapshot devices advertise no polled objects.
             * Poll does not consume relative axes or buffered events. */
            hr=o->acquired?1:o->lost?0x8007001eu:DI_NOTACQUIRED;}}
    fnv_host_mutex_unlock(&active->guard);fnv_cpu_return_u32(hr,4);
}
static void api_caps(void){
    uint32_t self=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1);if(!current())return;
    unsigned char *p=range(out,4,0);if(!p)return;uint32_t bytes=rd(p);
    if(bytes!=24 && bytes!=44){fnv_cpu_return_u32(DI_INVALID,8);return;}
    p=range(out,bytes,1);if(!p)return;unsigned char data[44]={0};uint32_t hr=0;
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {uint32_t kind=active->objects[i].kind;
        wr(data,bytes);wr(data+4,5);wr(data+8,kind==1?19:18);wr(data+12,kind==1?0:3);wr(data+16,kind==1?256:8);}
    fnv_host_mutex_unlock(&active->guard);if(!hr)memcpy(p,data,bytes);fnv_cpu_return_u32(hr,8);
}
static void property(int get){
    uint32_t self=fnv_cpu_argument_u32(0),id=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);if(!current())return;
    if(id!=1){recomp_unimpl("DirectInput property pending",id);return;}
    unsigned char *p=range(at,20,get);if(!p)return;
    if(rd(p)!=20 || rd(p+4)!=16 || rd(p+8) || rd(p+12)){fnv_cpu_return_u32(DI_INVALID,12);return;}
    uint32_t requested=get?0:rd(p+16),capacity=requested>4096?4096:requested,hr=0;
    struct di_event *events=capacity?calloc(capacity,sizeof(*events)):NULL;
    if(capacity && !events){fnv_cpu_return_u32(0x8007000e,12);return;}
    fnv_host_mutex_lock(&active->guard);int i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {struct di_object *o=active->objects+i;
        if(get)requested=o->requested;
        else if(o->acquired)hr=DI_ACQUIRED;
        else {free(o->events);o->events=events;events=NULL;o->requested=requested;o->capacity=capacity;o->head=o->queued=o->overflow=0;}}
    fnv_host_mutex_unlock(&active->guard);free(events);if(get && !hr)wr(p+16,requested);fnv_cpu_return_u32(hr,12);
}
static void api_get_property(void){property(1);}static void api_set_property(void){property(0);}
static void api_data(void){
    uint32_t self=fnv_cpu_argument_u32(0),stride=fnv_cpu_argument_u32(1),out=fnv_cpu_argument_u32(2),count_at=fnv_cpu_argument_u32(3),flags=fnv_cpu_argument_u32(4);if(!current())return;
    unsigned char *np=range(count_at,4,1);if(!np)return;uint32_t requested=rd(np);
    if(stride!=20 || flags>1){fnv_cpu_return_u32(DI_INVALID,20);return;}
    struct di_object before={0};fnv_host_mutex_lock(&active->guard);int i=index_of(self);if(i>=0)before=active->objects[i];fnv_host_mutex_unlock(&active->guard);
    int focus=before.acquired && (before.cooperative&4)?focused(before.window):1;
    /* Bound guest validation to the maximum actual copy; UINT_MAX is a valid
     * request to flush/query the whole queue, never a multi-gigabyte probe. */
    uint32_t limit=requested<before.queued?requested:before.queued;unsigned char *p=out && limit?range(out,limit*stride,1):NULL;
    if(out && limit && !p)return;
    uint32_t hr=0,n=0;fnv_host_mutex_lock(&active->guard);i=index_of(self);
    if(i<0 || !active->objects[i].kind)hr=DI_INVALID;
    else {struct di_object *o=active->objects+i;
        if(o->cooperative!=before.cooperative || o->window!=before.window || o->format!=before.format)hr=DI_INVALID;
        else {
            if(o->acquired && !focus){o->acquired=0;o->lost=1;o->head=o->queued=o->overflow=0;}
            if(!o->acquired)hr=o->lost?0x8007001eu:DI_NOTACQUIRED;
            else if(!o->capacity)hr=0x80040207u;
            else {n=requested<o->queued?requested:o->queued;if(out && n>limit)n=limit;hr=o->overflow?1:0;
                if(p)for(unsigned j=0;j<n;j++){struct di_event *e=o->events+(o->head+j)%o->capacity;
                    wr(p+j*stride,e->offset);wr(p+j*stride+4,e->data);wr(p+j*stride+8,e->timestamp);wr(p+j*stride+12,e->sequence);
                    if(stride==20)wr(p+j*stride+16,0);}
                if(!flags){o->head=(o->head+n)%o->capacity;o->queued-=n;o->overflow=0;}}}}
    fnv_host_mutex_unlock(&active->guard);if(!hr || hr==1)wr(np,n);fnv_cpu_return_u32(hr,20);
}
static void event(struct fnv_dinput8 *d,struct di_object *o,uint32_t offset,uint32_t data,uint32_t timestamp){
    if(!o->capacity || !o->acquired || !o->format)return;
    if(o->queued==o->capacity){o->head=(o->head+1)%o->capacity;o->queued--;o->overflow=1;}
    o->events[(o->head+o->queued++)%o->capacity]=(struct di_event){offset,data,timestamp,d->sequence++};
}
#define PENDING(N) static void pending_##N(void){recomp_unimpl("DirectInput COM method pending",N);}
PENDING(6) PENDING(7) PENDING(8) PENDING(9) PENDING(10)
PENDING(15) PENDING(23)
PENDING(25) PENDING(26) PENDING(27) PENDING(28) PENDING(29) PENDING(30) PENDING(31)
PENDING(32) PENDING(33) PENDING(34) PENDING(35) PENDING(37) PENDING(38)
PENDING(39) PENDING(40) PENDING(41) PENDING(42)
static fnv_cpu_function methods[DI_METHODS]={api_query,api_addref,api_release,api_create_device,api_enum,api_status,pending_6,pending_7,pending_8,pending_9,pending_10,
    api_query,api_addref,api_release,api_caps,pending_15,api_get_property,api_set_property,api_acquire,api_unacquire,api_state,api_data,api_format,pending_23,api_cooperative,
    pending_25,pending_26,pending_27,pending_28,pending_29,pending_30,pending_31,pending_32,pending_33,pending_34,pending_35,api_poll,pending_37,pending_38,pending_39,pending_40,pending_41,pending_42};
static const struct fnv_cpu_export exports[]={{"dinput8.dll","DirectInput8Create",0,0,api_factory}};
const struct fnv_cpu_export *fnv_dinput8_exports(size_t *n){if(n)*n=1;return exports;}
enum fnv_cpu_result fnv_dinput8_create(struct fnv_windows_process *process,const struct fnv_dinput8_config *cfg,struct fnv_dinput8 **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(!process || !cfg || !cfg->instance || (cfg->objects_address&3) || cfg->objects_bytes<4096 || (!cfg->keyboard && !cfg->mouse))return FNV_CPU_INVALID;
    if(active)return FNV_CPU_BUSY;
    void *memory;struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);
    enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,cfg->objects_address,cfg->objects_bytes,&memory);if(r)return r;
    if(cfg->memory){unsigned char *data;uint32_t base,bytes;void *backing;
        if(!fnv_vm_binding(cfg->memory,&data,&base,&bytes) || fnv_cpu_prepare_memory(cpu,base,bytes,&backing)!=FNV_CPU_OK || backing!=data ||
           !fnv_vm_access(cfg->memory,cfg->objects_address,cfg->objects_bytes,1))return FNV_CPU_INVALID;}

    struct fnv_dinput8 *d=calloc(1,sizeof(*d));if(!d)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&d->guard)){free(d);return FNV_CPU_NO_MEMORY;}
    d->cpu=cpu;d->config=*cfg;d->memory=memory;
    if(cfg->memory && !fnv_vm_retain(cfg->memory)){fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_INVALID;}
    r=fnv_cpu_register_native(cpu,methods,DI_METHODS,d->tokens);
    if(r){if(cfg->memory)fnv_vm_release_reference(cfg->memory);fnv_host_mutex_destroy(&d->guard);free(d);return r;}
    for(unsigned i=0;i<DI_METHODS;i++)wr(d->memory+i*4,d->tokens[i]);
    active=d;*out=d;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_dinput8_destroy(struct fnv_dinput8 *d){
    if(!d || d!=active)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(d->cpu,d->tokens[0],DI_METHODS);if(r)return r;
    active=NULL;for(unsigned i=0;i<d->count;i++)free(d->objects[i].events);
    if(d->config.memory)fnv_vm_release_reference(d->config.memory);
    fnv_host_mutex_destroy(&d->guard);free(d);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_dinput8_update(struct fnv_dinput8 *d,const struct fnv_di_snapshot *s){
    if(!d || d!=active || !s)return FNV_CPU_INVALID;
    for(unsigned i=0;i<256;i++)if(s->keys[i]!=0 && s->keys[i]!=128)return FNV_CPU_INVALID;
    for(unsigned i=0;i<8;i++)if(s->buttons[i]!=0 && s->buttons[i]!=128)return FNV_CPU_INVALID;
    uint64_t timestamp;if(!fnv_host_uptime_ms(&timestamp))return FNV_CPU_INVALID;
    int32_t inc[]={s->dx,s->dy,s->wheel};fnv_host_mutex_lock(&d->guard);
    for(unsigned i=0;i<3;i++)if((inc[i]>0 && d->totals[i]>INT64_MAX-inc[i]) || (inc[i]<0 && d->totals[i]<INT64_MIN-inc[i])){
        fnv_host_mutex_unlock(&d->guard);return FNV_CPU_INVALID;}
    for(unsigned i=0;i<d->count;i++){struct di_object *o=d->objects+i;if(!o->refs || !o->acquired || !o->capacity)continue;
        if(o->kind==1){for(unsigned k=0;k<256;k++)if(d->snapshot.keys[k]!=s->keys[k])event(d,o,k,s->keys[k],(uint32_t)timestamp);}
        else if(o->kind==2){for(unsigned k=0;k<3;k++)if(inc[k])event(d,o,k*4,(uint32_t)inc[k],(uint32_t)timestamp);
            for(unsigned k=0;k<o->format-12;k++)if(d->snapshot.buttons[k]!=s->buttons[k])event(d,o,12+k,s->buttons[k],(uint32_t)timestamp);}}
    d->snapshot=*s;for(unsigned i=0;i<3;i++)d->totals[i]+=inc[i];fnv_host_mutex_unlock(&d->guard);return FNV_CPU_OK;
}
