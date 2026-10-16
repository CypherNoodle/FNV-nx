#include "fnv_sdk_runtime.h"
#include "d3d9_com.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>

enum { DEVICE_SLOTS=119,METHODS=FNV_D3D9_COM_METHOD_COUNT,FACTORY_VTABLE=768,FACTORY_OBJECT=1016,OBJECT_OFFSET=1024,BUFFER_VTABLE=(119+5)*4,DECL_VTABLE=(119+5+14)*4,TEXTURE_VTABLE=(119+5+14+5)*4,SURFACE_VTABLE=(119+5+14+5+22)*4 };
enum { E_POINTER_VALUE=0x80004003u,E_NOINTERFACE_VALUE=0x80004002u };
struct code { unsigned char *bytes;uint32_t size;enum fnv_d3d9_stage stage; };
struct mip {uint32_t width,height,pitch,offset,locked,flags,left,top,right,bottom;};
struct resource {
    uintptr_t handle;uint32_t references,pins;struct code *code;enum fnv_d3d9_resource_kind kind;
    uint32_t data,size,usage,format,fvf,pool,locked,lock_offset,lock_bytes,lock_flags;
    uint64_t version;
    unsigned char *elements;uint32_t element_count;
    struct mip *mips;uint32_t levels,block,unit;
    uint32_t surface_desc[8];
};
struct span {uint32_t offset,bytes;};
struct stream {int index;uint32_t offset,stride,frequency;};
struct fnv_d3d9_com {
    struct fnv_cpu_runtime *cpu;
    struct fnv_d3d9_com_config config;
    struct fnv_d3d9_constants *constants;
    struct code *catalog;
    struct resource *resources;
    unsigned char *objects;
    unsigned char *buffer_memory;struct span *free_spans;uint32_t free_count;
    int shaders[2],indices,declaration,textures[16],targets[4],depth_target;struct stream streams[16];
    struct fnv_d3d9_factory_profile factory;uint32_t factory_refs,factory_created,creation[4];
    uint32_t tokens[METHODS],used,device_references;int in_scene;
    fnv_host_mutex guard;
};
static struct fnv_d3d9_com *active_com;
static const unsigned char iid_factory[16]={0xca,0xcb,0xbd,0x81,0xd4,0x64,0x6d,0x42,0xae,0x8d,0xad,0x01,0x47,0xf4,0x27,0x5c};
static const unsigned char iid_surface[16]={0x3A,0xAF,0xFB,0x0C,0xF6,0x9F,0x9A,0x42,0x99,0xB3,0xA2,0x79,0x6A,0xF8,0xB8,0x9B};
static const unsigned char iid_texture[16]={0x27,0x12,0xC3,0x85,0xE5,0x3D,0x00,0x4F,0x9B,0x3A,0xF1,0x1A,0xC3,0x8C,0x18,0xB5};
static const unsigned char iid_base_texture[16]={0x7E,0xA8,0x0C,0x58,0x3C,0x1D,0x54,0x4D,0x99,0x1D,0xB7,0xD3,0xE3,0xC2,0x98,0xCE};
static const unsigned char iid_unknown[16]={0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46};
static const unsigned char iid_device[16]={0x96,0x3B,0x22,0xD0,0x7A,0xBF,0xFD,0x43,0x92,0xBD,0xA4,0x3B,0x0D,0x82,0xB9,0xEB};
static const unsigned char iid_vertex[16]={0x7E,0x55,0xC5,0xEF,0x65,0x62,0x13,0x46,0x8A,0x94,0x43,0x85,0x78,0x89,0xEB,0x36};
static const unsigned char iid_pixel[16]={0xDC,0xDB,0x3B,0x6D,0x02,0x5B,0x15,0x44,0xB8,0x52,0xCE,0x5E,0x8B,0xCC,0xB2,0x89};
static const unsigned char iid_declaration[16]={0x9C,0xC5,0x13,0xDD,0xFA,0x36,0x98,0x40,0xA8,0xFB,0xC7,0xED,0x39,0xDC,0x85,0x46};
static const unsigned char iid_resource[16]={0x5D,0xC0,0xEE,0x05,0x7D,0x8F,0x62,0x43,0xB9,0x99,0xD1,0xBA,0xF3,0x57,0xC7,0x04};
static const unsigned char iid_vertex_buffer[16]={0xB5,0xB1,0x4B,0xB6,0x70,0xFD,0xF6,0x4D,0xBF,0x91,0x19,0xD0,0xA1,0x24,0x55,0xE3};
static const unsigned char iid_index_buffer[16]={0x5E,0xD6,0x9D,0x7C,0xF7,0xD3,0x29,0x45,0xAC,0xEE,0x78,0x58,0x30,0xAC,0xDE,0x35};
static uint32_t read32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void write32(unsigned char *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static _Noreturn void invalid(const char *message,uint32_t address){recomp_unimpl(message,address);abort();}
static struct fnv_d3d9_com *module(void) {
    if(!active_com || fnv_cpu_current()!=active_com->cpu)invalid("D3D9 module/context unavailable",0);
    return active_com;
}
static void *range(struct fnv_d3d9_com *c,uint32_t at,uint32_t bytes,int write_access) {
    if(!at)return NULL;
    void *p=fnv_cpu_try_memory(at,bytes);if(!p)return NULL;
    unsigned char *backing;uint32_t base,size;
    if(fnv_vm_binding(c->config.memory,&backing,&base,&size) &&
       (uint64_t)at<(uint64_t)base+size && (uint64_t)at+bytes>base &&
       !fnv_vm_access(c->config.memory,at,bytes,write_access))return NULL;
    return p;
}
static uint32_t device_address(const struct fnv_d3d9_com *c){return c->config.objects_address+OBJECT_OFFSET-4;}
static uint32_t resource_address(const struct fnv_d3d9_com *c,uint32_t index){return c->config.objects_address+OBJECT_OFFSET+index*4;}
/* Return -1 for device, otherwise a live resource index. Failure unlocks before
 * unwinding to the CPU boundary. Guest pointer probes happen before locking. */
static int object(struct fnv_d3d9_com *c,uint32_t at,int device_only) {
    if(!device_only && c->config.factory && c->factory_refs && at==c->config.objects_address+FACTORY_OBJECT && read32(c->objects+FACTORY_OBJECT)==c->config.objects_address+FACTORY_VTABLE)return -2;
    if(at==device_address(c) && c->device_references && read32(c->objects+OBJECT_OFFSET-4)==c->config.objects_address)return -1;
    if(!device_only && at>=resource_address(c,0) && !((at-resource_address(c,0))&3)) {
        uint32_t index=(at-resource_address(c,0))/4;
        if(index<c->used && c->resources[index].references &&
           read32(c->objects+OBJECT_OFFSET+index*4)==c->config.objects_address+(c->resources[index].kind<2?DEVICE_SLOTS*4:c->resources[index].kind==FNV_D3D9_DECL?DECL_VTABLE:c->resources[index].kind==FNV_D3D9_TEXTURE2D?TEXTURE_VTABLE:c->resources[index].kind==FNV_D3D9_SURFACE?SURFACE_VTABLE:BUFFER_VTABLE))return (int)index;
    }
    fnv_host_mutex_unlock(&c->guard);invalid("D3D9 invalid/stale this pointer",at);
}
static uint32_t allocate_shadow(struct fnv_d3d9_com *c,uint32_t bytes) {
    uint64_t aligned=((uint64_t)bytes+15)&~UINT64_C(15);if(aligned>c->config.buffer_bytes)return 0;
    for(uint32_t i=0;i<c->free_count;i++)if(c->free_spans[i].bytes>=aligned) {
        uint32_t at=c->config.buffer_address+c->free_spans[i].offset;
        c->free_spans[i].offset+=(uint32_t)aligned;c->free_spans[i].bytes-=(uint32_t)aligned;
        if(!c->free_spans[i].bytes){c->free_count--;memmove(c->free_spans+i,c->free_spans+i+1,(c->free_count-i)*sizeof(*c->free_spans));}
        memset(c->buffer_memory+(at-c->config.buffer_address),0,bytes);return at;
    }
    return 0;
}
static void retire(struct fnv_d3d9_com *c,int index) {
    struct resource *r=c->resources+index;if(r->references || r->pins)return;
    r->handle=0;free(r->mips);r->mips=NULL;free(r->elements);r->elements=NULL;r->element_count=0;
    if(r->data) {
        struct span s={r->data-c->config.buffer_address,(r->size+15u)&~15u};uint32_t i=0;
        while(i<c->free_count && c->free_spans[i].offset<s.offset)i++;
        memmove(c->free_spans+i+1,c->free_spans+i,(c->free_count-i)*sizeof(*c->free_spans));c->free_spans[i]=s;c->free_count++;
        if(i && c->free_spans[i-1].offset+c->free_spans[i-1].bytes==s.offset){c->free_spans[i-1].bytes+=s.bytes;
            memmove(c->free_spans+i,c->free_spans+i+1,(--c->free_count-i)*sizeof(*c->free_spans));i--;}
        if(i+1<c->free_count && c->free_spans[i].offset+c->free_spans[i].bytes==c->free_spans[i+1].offset){c->free_spans[i].bytes+=c->free_spans[i+1].bytes;
            memmove(c->free_spans+i+1,c->free_spans+i+2,(--c->free_count-i-1)*sizeof(*c->free_spans));}
        r->data=0;
    }
}
static void clear_bindings(struct fnv_d3d9_com *c) {
    c->shaders[0]=c->shaders[1]=c->indices=c->declaration=-1;
    for(unsigned i=0;i<16;i++){c->streams[i].index=-1;c->textures[i]=-1;}
    c->depth_target=-1;for(unsigned i=0;i<4;i++)c->targets[i]=-1;
    for(uint32_t i=0;i<c->used;i++){c->resources[i].pins=0;retire(c,(int)i);}
}
static void increment(struct fnv_d3d9_com *c,int index) {
    if(index==-2){c->factory_refs++;return;}
    if(index<0){c->device_references++;c->config.backend.device_addref(c->config.backend.context);}
    else {if(!c->resources[index].references)c->device_references++;
        c->resources[index].references++;c->config.backend.addref(c->config.backend.context,c->resources[index].handle);}
}
static uint32_t decrement(struct fnv_d3d9_com *c,int index) {
    if(index==-2)return --c->factory_refs;
    if(index<0){c->config.backend.device_release(c->config.backend.context);if(!--c->device_references){clear_bindings(c);if(c->config.factory)c->factory_refs--;}return c->device_references;}
    struct resource *s=c->resources+index;c->config.backend.release(c->config.backend.context,s->handle);
    if(!--s->references){c->device_references--;retire(c,index);if(!c->device_references){clear_bindings(c);if(c->config.factory)c->factory_refs--;}}
    return s->references;
}
static void api_query(void) {
    uint32_t self=fnv_cpu_argument_u32(0),iid_at=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1),*iid=range(c,iid_at,16,0);
    if(!out || !iid){fnv_cpu_return_u32(E_POINTER_VALUE,12);return;}
    unsigned char requested[16];memcpy(requested,iid,16);
    fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    const unsigned char *supported=index==-2?iid_factory:index<0?iid_device:c->resources[index].kind==FNV_D3D9_VS?iid_vertex:
        c->resources[index].kind==FNV_D3D9_PS?iid_pixel:c->resources[index].kind==FNV_D3D9_VB?iid_vertex_buffer:
        c->resources[index].kind==FNV_D3D9_IB?iid_index_buffer:
        c->resources[index].kind==FNV_D3D9_DECL?iid_declaration:
        c->resources[index].kind==FNV_D3D9_TEXTURE2D?iid_texture:iid_surface;
    uint32_t result=E_NOINTERFACE_VALUE;write32(out,0);
    if(!memcmp(requested,iid_unknown,16) || !memcmp(requested,supported,16) ||
       (index>=0 && c->resources[index].kind==FNV_D3D9_TEXTURE2D && !memcmp(requested,iid_base_texture,16)) ||
       (index>=0 && (c->resources[index].kind==FNV_D3D9_VB || c->resources[index].kind==FNV_D3D9_IB || c->resources[index].kind==FNV_D3D9_TEXTURE2D || c->resources[index].kind==FNV_D3D9_SURFACE) && !memcmp(requested,iid_resource,16))) {
        uint32_t refs=index==-2?c->factory_refs:index<0?c->device_references:c->resources[index].references;
        if(refs==UINT32_MAX){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 reference overflow",self);}
        increment(c,index);write32(out,self);result=0;
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_addref(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    uint32_t refs=index==-2?c->factory_refs:index<0?c->device_references:c->resources[index].references;
    if(refs==UINT32_MAX){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 reference overflow",self);}
    increment(c,index);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(refs+1,4);
}
static void api_release(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    uint32_t refs=decrement(c,index);fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(refs,4);
}
static void api_get_device(void) {
    uint32_t self=fnv_cpu_argument_u32(0),out_at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);
    if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    if(index<0 || c->device_references==UINT32_MAX){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetDevice invalid object/count",self);}
    increment(c,-1);write32(out,device_address(c));fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(0,8);
}
static void api_get_function(void) {
    uint32_t self=fnv_cpu_argument_u32(0),data_at=fnv_cpu_argument_u32(1),size_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *size=range(c,size_at,4,1);
    if(!size){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    if(index<0 || c->resources[index].kind>=2){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetFunction requires shader",self);}
    struct code *code=c->resources[index].code;fnv_host_mutex_unlock(&c->guard);
    /* Windows NULLREF writes the complete bytecode even when the incoming
     * size value is smaller. Validate the actual destination extent, not that
     * value. Catalog bytes remain immutable for the whole module lifetime. */
    void *data=data_at?range(c,data_at,code->size,1):NULL;
    if(data_at && !data){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    if(data)memcpy(data,code->bytes,code->size);
    write32(size,code->size);fnv_cpu_return_u32(0,12);
}
static void create_shader(enum fnv_d3d9_stage stage) {
    uint32_t self=fnv_cpu_argument_u32(0),code_at=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);
    if(!out || !range(c,code_at,4,0)){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    struct code *code=NULL;
    for(size_t i=0;i<c->config.catalog_count;i++)if(c->catalog[i].stage==stage) {
        void *bytes=range(c,code_at,c->catalog[i].size,0);
        if(bytes && !memcmp(bytes,c->catalog[i].bytes,c->catalog[i].size)){code=c->catalog+i;break;}
    }
    if(!code)invalid("D3D9 shader outside prepared catalog",code_at);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);write32(out,0);
    uintptr_t handle=0;uint32_t result=c->config.backend.create(c->config.backend.context,stage,code->bytes,code->size,&handle);
    if(!result && !handle){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 backend returned no shader",code_at);}
    if(!result) {
        uint32_t index;
        for(index=0;index<c->used;index++)if(c->resources[index].handle==handle)break;
        if(index<c->used && (c->resources[index].kind>=2 || c->resources[index].code->size!=code->size ||
           memcmp(c->resources[index].code->bytes,code->bytes,code->size))) {
            c->config.backend.release(c->config.backend.context,handle);fnv_host_mutex_unlock(&c->guard);
            invalid("D3D9 backend reused a different live shader identity",code_at);
        }
        if(index==c->used) {
            for(index=0;index<c->used;index++)if(!c->resources[index].handle)break;
            if((index==c->used && c->used==c->config.resource_capacity) || c->device_references==UINT32_MAX){
                c->config.backend.release(c->config.backend.context,handle);result=FNV_D3D9_OUTOFMEMORY;
            } else {c->resources[index]=(struct resource){.handle=handle,.code=code,.kind=(enum fnv_d3d9_resource_kind)stage};if(index==c->used)c->used++;
                write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+DEVICE_SLOTS*4);}
        }
        if(!result) {
            if(c->resources[index].references==UINT32_MAX){c->config.backend.release(c->config.backend.context,handle);result=FNV_D3D9_OUTOFMEMORY;}
            else {if(!c->resources[index].references)c->device_references++;
                c->resources[index].references++;write32(out,resource_address(c,index));}
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_create_vertex(void){create_shader(FNV_D3D9_VERTEX);}
static void api_create_pixel(void){create_shader(FNV_D3D9_PIXEL);}
static int resource_kind(struct fnv_d3d9_com *c,uint32_t at,enum fnv_d3d9_resource_kind kind) {
    int index=object(c,at,0);
    return index>=0 && c->resources[index].kind==kind?index:-1;
}
static void change_binding(struct fnv_d3d9_com *c,int *slot,int index) {
    if(*slot==index)return;
    if(*slot>=0){c->resources[*slot].pins--;retire(c,*slot);}
    *slot=index;if(index>=0)c->resources[index].pins++;
}
static void get_binding(struct fnv_d3d9_com *c,int index,unsigned char *out) {
    if(index<0){write32(out,0);return;}
    struct resource *r=c->resources+index;
    if(r->references==UINT32_MAX || (!r->references && c->device_references==UINT32_MAX)) {
        fnv_host_mutex_unlock(&c->guard);invalid("D3D9 bound resource reference overflow",resource_address(c,index));
    }
    increment(c,index);write32(out,resource_address(c,index));
}
static void shader_binding(enum fnv_d3d9_stage stage,int get) {
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();
    unsigned char *out=get?range(c,at,4,1):NULL;
    if(get && !out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    if(!get && !c->config.backend.bind_shader)invalid("D3D9 shader binding provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=0;
    if(get)get_binding(c,c->shaders[stage],out);
    else {int index=at?resource_kind(c,at,(enum fnv_d3d9_resource_kind)stage):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.bind_shader(c->config.backend.context,stage,index<0?0:c->resources[index].handle);
            if(!result)change_binding(c,c->shaders+stage,index);}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
}
static void api_set_vs(void){shader_binding(FNV_D3D9_VERTEX,0);}
static void api_get_vs(void){shader_binding(FNV_D3D9_VERTEX,1);}
static void api_set_ps(void){shader_binding(FNV_D3D9_PIXEL,0);}
static void api_get_ps(void){shader_binding(FNV_D3D9_PIXEL,1);}
static void create_buffer(enum fnv_d3d9_resource_kind kind) {
    uint32_t self=fnv_cpu_argument_u32(0),bytes=fnv_cpu_argument_u32(1),usage=fnv_cpu_argument_u32(2);
    uint32_t format=fnv_cpu_argument_u32(3),pool=fnv_cpu_argument_u32(4),out_at=fnv_cpu_argument_u32(5),shared=fnv_cpu_argument_u32(6);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);
    if(!out || !bytes || (kind==FNV_D3D9_IB && format!=101 && format!=102)){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,28);return;}
    if(shared)invalid("D3D9 shared buffer handles pending",shared);
    if(!c->config.backend.create_buffer || !c->config.backend.update_buffer || !c->buffer_memory)invalid("D3D9 buffer provider/arena pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);write32(out,0);uint32_t index;
    for(index=0;index<c->used;index++)if(!c->resources[index].handle)break;
    uint32_t result=FNV_D3D9_OUTOFMEMORY;
    if(index<c->config.resource_capacity && c->device_references<UINT32_MAX) {
        uint32_t address=allocate_shadow(c,bytes);
        if(address) {
            if(index==c->used)c->used++;
            struct resource *r=c->resources+index;*r=(struct resource){.kind=kind,.data=address,.size=bytes,.usage=usage,
                .format=kind==FNV_D3D9_VB?100:format,.fvf=kind==FNV_D3D9_VB?format:0,.pool=pool};
            uintptr_t handle=0;result=c->config.backend.create_buffer(c->config.backend.context,kind,bytes,usage,format,pool,&handle);
            if(!result && !handle){retire(c,(int)index);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 backend returned no buffer",self);}
            if(!result) {
                for(uint32_t i=0;i<c->used;i++)if(i!=index && c->resources[i].handle==handle){
                    c->config.backend.release(c->config.backend.context,handle);retire(c,(int)index);fnv_host_mutex_unlock(&c->guard);
                    invalid("D3D9 backend reused a live buffer identity",self);}
                r->handle=handle;r->references=1;c->device_references++;
                write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+BUFFER_VTABLE);write32(out,resource_address(c,index));
            } else retire(c,(int)index);
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,28);
}
static void api_create_vb(void){create_buffer(FNV_D3D9_VB);}
static void api_create_ib(void){create_buffer(FNV_D3D9_IB);}
static int buffer_object(struct fnv_d3d9_com *c,uint32_t self) {
    int index=object(c,self,0);
    if(index<0 || (c->resources[index].kind!=FNV_D3D9_VB && c->resources[index].kind!=FNV_D3D9_IB)){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 buffer method requires buffer",self);}
    return index;
}
static void api_buffer_type(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);
    int index=object(c,self,0);
    if(index<0 || (c->resources[index].kind!=FNV_D3D9_VB && c->resources[index].kind!=FNV_D3D9_IB && c->resources[index].kind!=FNV_D3D9_TEXTURE2D && c->resources[index].kind!=FNV_D3D9_SURFACE)){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetType requires resource",self);}
    uint32_t type=c->resources[index].kind==FNV_D3D9_SURFACE?1:c->resources[index].kind==FNV_D3D9_TEXTURE2D?3:c->resources[index].kind==FNV_D3D9_VB?6:7;
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(type,4);
}
static void api_buffer_desc(void) {
    uint32_t self=fnv_cpu_argument_u32(0),out_at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();
    fnv_host_mutex_lock(&c->guard);int index=buffer_object(c,self);struct resource r=c->resources[index];fnv_host_mutex_unlock(&c->guard);
    uint32_t count=r.kind==FNV_D3D9_VB?6:5;unsigned char *out=range(c,out_at,count*4,1);
    if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    uint32_t values[6]={r.format,r.kind==FNV_D3D9_VB?6:7,r.usage,r.pool,r.size,r.fvf};
    for(uint32_t i=0;i<count;i++)write32(out+i*4,values[i]);
    fnv_cpu_return_u32(0,8);
}
static void api_buffer_lock(void) {
    uint32_t self=fnv_cpu_argument_u32(0),offset=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2);
    uint32_t out_at=fnv_cpu_argument_u32(3),flags=fnv_cpu_argument_u32(4);struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);
    if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,20);return;}
    if(flags&~0xB810u)invalid("D3D9 buffer lock flags pending",flags);
    fnv_host_mutex_lock(&c->guard);int index=buffer_object(c,self);struct resource *r=c->resources+index;uint32_t result=0;
    if(r->locked){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 nested buffer locks pending",self);}
    if(offset>r->size || bytes>r->size-offset || ((flags&0x3000) && !(r->usage&0x200)) ||
       ((flags&0x10) && ((r->usage&8) || (flags&0x3000))) || (flags&0x3000)==0x3000)result=FNV_D3D9_INVALIDCALL;
    else {if(!bytes)bytes=r->size-offset;
        if(flags&0x2000)memset(c->buffer_memory+(r->data-c->config.buffer_address),0,r->size);
        r->locked=1;r->lock_offset=offset;r->lock_bytes=bytes;r->lock_flags=flags;write32(out,r->data+offset);}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,20);
}
static void api_buffer_unlock(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);
    int index=buffer_object(c,self);struct resource *r=c->resources+index;uint32_t result=0;
    if(r->locked) {
        if(!(r->lock_flags&0x10) && r->lock_bytes)result=c->config.backend.update_buffer(c->config.backend.context,r->handle,r->lock_offset,r->lock_bytes,r->lock_flags,
            c->buffer_memory+(r->data-c->config.buffer_address)+r->lock_offset);
        if(!result){if(!(r->lock_flags&0x10))r->version++;r->locked=0;}
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,4);
}
static void stream_binding(int get) {
    uint32_t self=fnv_cpu_argument_u32(0),stream=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2),offset=fnv_cpu_argument_u32(3),stride=fnv_cpu_argument_u32(4);
    struct fnv_d3d9_com *c=module();unsigned char *out=get?range(c,at,4,1):NULL,*off=get?range(c,offset,4,1):NULL,*step=get?range(c,stride,4,1):NULL;
    if(stream>=16 || (get && (!out || !off || !step))){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,20);return;}
    if(!get && !c->config.backend.bind_stream)invalid("D3D9 stream provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);struct stream *s=c->streams+stream;uint32_t result=0;
    if(get){get_binding(c,s->index,out);write32(off,s->offset);write32(step,s->stride);}
    else {int index=at?resource_kind(c,at,FNV_D3D9_VB):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.bind_stream(c->config.backend.context,stream,index<0?0:c->resources[index].handle,offset,stride);
            if(!result){change_binding(c,&s->index,index);s->offset=offset;s->stride=stride;}}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,20);
}
static void api_set_stream(void){stream_binding(0);}
static void api_get_stream(void){stream_binding(1);}
static void index_binding(int get) {
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();unsigned char *out=get?range(c,at,4,1):NULL;
    if(get && !out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    if(!get && !c->config.backend.bind_indices)invalid("D3D9 index provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=0;
    if(get)get_binding(c,c->indices,out);
    else {int index=at?resource_kind(c,at,FNV_D3D9_IB):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.bind_indices(c->config.backend.context,index<0?0:c->resources[index].handle);
            if(!result)change_binding(c,&c->indices,index);}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
}
static void api_set_indices(void){index_binding(0);}
static void api_get_indices(void){index_binding(1);}
static void frequency(int get) {
    uint32_t self=fnv_cpu_argument_u32(0),stream=fnv_cpu_argument_u32(1),value=fnv_cpu_argument_u32(2);struct fnv_d3d9_com *c=module();unsigned char *out=get?range(c,value,4,1):NULL;
    if(stream>=16 || (get && !out)){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    if(!get && !c->config.backend.stream_frequency)invalid("D3D9 frequency provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=0;
    if(get)write32(out,c->streams[stream].frequency);
    else {result=c->config.backend.stream_frequency(c->config.backend.context,stream,value);if(!result)c->streams[stream].frequency=value;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_set_frequency(void){frequency(0);}
static void api_get_frequency(void){frequency(1);}
static void api_create_declaration(void) {
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1),elements[65*8];uint32_t count=0;
    if(!out || !at){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    if(!c->config.backend.create_declaration)invalid("D3D9 declaration provider pending",self);
    /* Bounded copy, no unbounded END scan or host pointer in guest memory. */
    for(unsigned i=0;i<65;i++) {
        uint64_t address=(uint64_t)at+i*8;void *p=address<=UINT32_MAX?range(c,(uint32_t)address,8,0):NULL;
        if(!p)break;
        memcpy(elements+i*8,p,8);
        if(elements[i*8]==0xFF && elements[i*8+1]==0){count=i+1;break;}
    }
    if(!count){write32(out,0);fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);write32(out,0);
    uintptr_t handle=0;uint32_t result=c->config.backend.create_declaration(c->config.backend.context,elements,count,&handle);
    if(!result && !handle){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 backend returned no declaration",at);}
    if(!result) {
        uint32_t index;
        for(index=0;index<c->used;index++)if(c->resources[index].handle==handle)break;
        if(index<c->used && (c->resources[index].kind!=FNV_D3D9_DECL || c->resources[index].element_count!=count ||
            memcmp(c->resources[index].elements,elements,count*8))) {
            c->config.backend.release(c->config.backend.context,handle);fnv_host_mutex_unlock(&c->guard);
            invalid("D3D9 backend reused a different declaration identity",at);
        }
        if(index==c->used) {
            for(index=0;index<c->used;index++)if(!c->resources[index].handle)break;
            if(index==c->config.resource_capacity || c->device_references==UINT32_MAX)result=FNV_D3D9_OUTOFMEMORY;
            else {
                unsigned char *copy=malloc(count*8);
                if(!copy)result=FNV_D3D9_OUTOFMEMORY;
                else {memcpy(copy,elements,count*8);c->resources[index]=(struct resource){.handle=handle,.kind=FNV_D3D9_DECL,.elements=copy,.element_count=count};
                    if(index==c->used)c->used++;
                    write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+DECL_VTABLE);}
            }
        }
        if(!result) {
            struct resource *r=c->resources+index;
            if(r->references==UINT32_MAX || (!r->references && c->device_references==UINT32_MAX))result=FNV_D3D9_OUTOFMEMORY;
            else {if(!r->references)c->device_references++;r->references++;write32(out,resource_address(c,index));}
        }
        if(result)c->config.backend.release(c->config.backend.context,handle);
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void declaration_binding(int get) {
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();
    unsigned char *out=get?range(c,at,4,1):NULL;
    if(get && !out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    if(!get && !c->config.backend.bind_declaration)invalid("D3D9 declaration binding provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=0;
    if(get)get_binding(c,c->declaration,out);
    else {int index=at?resource_kind(c,at,FNV_D3D9_DECL):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.bind_declaration(c->config.backend.context,index<0?0:c->resources[index].handle);
            if(!result)change_binding(c,&c->declaration,index);}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
}
static void api_set_declaration(void){declaration_binding(0);}
static void api_get_declaration(void){declaration_binding(1);}
static void api_declaration_elements(void) {
    uint32_t self=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),count_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *count=range(c,count_at,4,1);
    if(!count){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    if(index<0 || c->resources[index].kind!=FNV_D3D9_DECL){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetDeclaration requires declaration",self);}
    struct resource *r=c->resources+index;void *out=at?range(c,at,r->element_count*8,1):NULL;
    uint32_t result=0;
    if(at && !out)result=FNV_D3D9_INVALIDCALL;
    else {if(out)memcpy(out,r->elements,r->element_count*8);write32(count,r->element_count);}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static int texture_format(uint32_t format,uint32_t *block,uint32_t *unit) {
    *block=1;
    switch(format){case 21:case 22:*unit=4;return 1;case 23:*unit=2;return 1;
        case 28:case 50:*unit=1;return 1;
        case 0x31545844:*block=4;*unit=8;return 1;
        case 0x32545844:case 0x33545844:case 0x34545844:case 0x35545844:*block=4;*unit=16;return 1;
        default:return 0;}
}
static int texture_object(struct fnv_d3d9_com *c,uint32_t at) {
    int index=object(c,at,0);
    if(index<0 || c->resources[index].kind!=FNV_D3D9_TEXTURE2D){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 texture method requires texture2D",at);}
    return index;
}
static void api_create_texture(void) {
    uint32_t self=fnv_cpu_argument_u32(0),width=fnv_cpu_argument_u32(1),height=fnv_cpu_argument_u32(2),levels=fnv_cpu_argument_u32(3);
    uint32_t usage=fnv_cpu_argument_u32(4),format=fnv_cpu_argument_u32(5),pool=fnv_cpu_argument_u32(6),out_at=fnv_cpu_argument_u32(7),shared=fnv_cpu_argument_u32(8);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);uint32_t block,unit;
    if(!out || !width || !height || width>16384 || height>16384){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,36);return;}
    if(shared || (usage&0x403u))invalid("D3D9 shared/render-target/depth/autogen textures pending",self);
    if(!texture_format(format,&block,&unit))invalid("D3D9 texture format pending",format);
    if(!c->config.backend.create_texture || !c->config.backend.upload_texture || !c->buffer_memory)invalid("D3D9 texture provider/arena pending",self);
    uint32_t maximum=1;for(uint32_t n=width>height?width:height;n>1;n>>=1)maximum++;
    if(!levels)levels=maximum;
    if(levels>maximum){write32(out,0);fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,36);return;}
    struct mip mips[15];uint64_t total=0;
    for(uint32_t i=0,w=width,h=height;i<levels;i++,w=w>1?w>>1:1,h=h>1?h>>1:1) {
        uint32_t pitch=(((w+block-1)/block)*unit+3)&~3u;
        mips[i]=(struct mip){.width=w,.height=h,.pitch=pitch,.offset=(uint32_t)total};
        total+=(uint64_t)pitch*((h+block-1)/block);
    }
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);write32(out,0);
    uint32_t index;for(index=0;index<c->used;index++)if(!c->resources[index].handle)break;
    uint32_t result=FNV_D3D9_OUTOFMEMORY;
    if(index<c->config.resource_capacity && total<=c->config.buffer_bytes && c->device_references<UINT32_MAX) {
        uint32_t address=allocate_shadow(c,(uint32_t)total);
        if(address) {
            if(index==c->used)c->used++;
            struct resource *r=c->resources+index;*r=(struct resource){.kind=FNV_D3D9_TEXTURE2D,.data=address,.size=(uint32_t)total,
                .usage=usage,.format=format,.pool=pool,.levels=levels,.block=block,.unit=unit};
            r->mips=malloc(levels*sizeof(*mips));
            if(r->mips){
                memcpy(r->mips,mips,levels*sizeof(*mips));uintptr_t handle=0;
                result=c->config.backend.create_texture(c->config.backend.context,width,height,levels,usage,format,pool,&handle);
                if(!result && !handle){retire(c,(int)index);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 backend returned no texture",self);}
                if(!result) {
                    for(uint32_t i=0;i<c->used;i++)if(i!=index && c->resources[i].handle==handle){
                        c->config.backend.release(c->config.backend.context,handle);retire(c,(int)index);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 backend reused live texture identity",self);}
                    r->handle=handle;r->references=1;c->device_references++;write32(out,resource_address(c,index));
                    write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+TEXTURE_VTABLE);
                }
            }
            if(result)retire(c,(int)index);
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,36);
}
static void api_texture_levels(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);
    uint32_t levels=c->resources[texture_object(c,self)].levels;fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(levels,4);
}
static void api_texture_desc(void) {
    uint32_t self=fnv_cpu_argument_u32(0),level=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2);struct fnv_d3d9_com *c=module();
    unsigned char *out=range(c,out_at,32,1);if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    fnv_host_mutex_lock(&c->guard);struct resource *r=c->resources+texture_object(c,self);uint32_t result=FNV_D3D9_INVALIDCALL;
    if(level<r->levels){uint32_t values[]={r->format,1,r->usage,r->pool,0,0,r->mips[level].width,r->mips[level].height};
        for(unsigned i=0;i<8;i++)write32(out+i*4,values[i]);
        result=0;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_texture_lock(void) {
    uint32_t self=fnv_cpu_argument_u32(0),level=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2),rect_at=fnv_cpu_argument_u32(3),flags=fnv_cpu_argument_u32(4);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,8,1),*rect=rect_at?range(c,rect_at,16,0):NULL;
    if(!out || (rect_at && !rect)){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,20);return;}
    if(flags&~0x2810u)invalid("D3D9 texture lock flags pending",flags);
    uint32_t bounds[4];if(rect)for(unsigned i=0;i<4;i++)bounds[i]=read32(rect+i*4);
    fnv_host_mutex_lock(&c->guard);struct resource *r=c->resources+texture_object(c,self);uint32_t result=FNV_D3D9_INVALIDCALL;
    if(level<r->levels) {
        struct mip *m=r->mips+level;
        if(m->locked){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 nested texture lock pending",self);}
        if(!rect){bounds[0]=bounds[1]=0;bounds[2]=m->width;bounds[3]=m->height;}
        int other_locked=0;if(flags&0x2000)for(uint32_t i=0;i<r->levels;i++)if(r->mips[i].locked)other_locked=1;
        if(bounds[0]<bounds[2] && bounds[1]<bounds[3] && bounds[2]<=m->width && bounds[3]<=m->height &&
            !(bounds[0]%r->block) && !(bounds[1]%r->block) && (bounds[2]==m->width || !(bounds[2]%r->block)) &&
            (bounds[3]==m->height || !(bounds[3]%r->block)) && (r->pool!=0 || (r->usage&0x200)) &&
            (!(flags&0x2000) || ((r->usage&0x200) && !rect_at && !level && !other_locked && !(flags&0x10)))) {
            if(flags&0x2000)memset(c->buffer_memory+(r->data-c->config.buffer_address),0,r->size);
            m->left=bounds[0];m->top=bounds[1];m->right=bounds[2];m->bottom=bounds[3];m->flags=flags;m->locked=1;
            write32(out,m->pitch);write32(out+4,r->data+m->offset+(m->top/r->block)*m->pitch+(m->left/r->block)*r->unit);result=0;
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,20);
}
static void api_texture_unlock(void) {
    uint32_t self=fnv_cpu_argument_u32(0),level=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);
    struct resource *r=c->resources+texture_object(c,self);uint32_t result=FNV_D3D9_INVALIDCALL;
    if(level<r->levels && r->mips[level].locked){struct mip *m=r->mips+level;result=0;
        if(!(m->flags&0x10))result=c->config.backend.upload_texture(c->config.backend.context,r->handle,level,m->left,m->top,m->right-m->left,m->bottom-m->top,m->flags,m->pitch,
            c->buffer_memory+(r->data-c->config.buffer_address)+m->offset+(m->top/r->block)*m->pitch+(m->left/r->block)*r->unit);
        if(!result){m->locked=0;if(!(m->flags&0x10))r->version++;}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,8);
}
static void texture_binding(int get) {
    uint32_t self=fnv_cpu_argument_u32(0),sampler=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);struct fnv_d3d9_com *c=module();
    unsigned char *out=get?range(c,at,4,1):NULL;
    if(sampler>=16)invalid("D3D9 non-pixel texture sampler pending",sampler);
    if(get && !out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,12);return;}
    if(!get && !c->config.backend.bind_texture)invalid("D3D9 texture binding provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=0;
    if(get)get_binding(c,c->textures[sampler],out);
    else {int index=at?resource_kind(c,at,FNV_D3D9_TEXTURE2D):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.bind_texture(c->config.backend.context,sampler,index<0?0:c->resources[index].handle);
            if(!result)change_binding(c,c->textures+sampler,index);}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_set_texture(void){texture_binding(0);}
static void api_get_texture(void){texture_binding(1);}
static void device_state(enum fnv_d3d9_state_kind kind,int get) {
    uint32_t self=fnv_cpu_argument_u32(0),sampler=0,name=0,values[6]={0},out_at=0,argc=2,words=1;
    struct fnv_d3d9_com *c=module();unsigned char *data=NULL;
    if(kind==FNV_D3D9_RENDER_STATE || kind==FNV_D3D9_SAMPLER_STATE) {
        argc=kind==FNV_D3D9_SAMPLER_STATE?4:3;
        if(kind==FNV_D3D9_SAMPLER_STATE)sampler=fnv_cpu_argument_u32(1);
        name=fnv_cpu_argument_u32(argc-2);out_at=fnv_cpu_argument_u32(argc-1);
        if(!get)values[0]=out_at;
    } else {words=kind==FNV_D3D9_VIEWPORT?6:4;out_at=fnv_cpu_argument_u32(1);}
    if(get || kind==FNV_D3D9_VIEWPORT || kind==FNV_D3D9_SCISSOR) {
        data=range(c,out_at,words*4,get);
        if(!data){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,argc*4);return;}
        if(!get)for(uint32_t i=0;i<words;i++)values[i]=read32(data+i*4);
    }
    if(!c->config.backend.state)invalid("D3D9 device state provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    uint32_t result=c->config.backend.state(c->config.backend.context,kind,sampler,name,values,get);
    if(!result && get)for(uint32_t i=0;i<words;i++)write32(data+i*4,values[i]);
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,argc*4);
}
static void api_set_render_state(void){device_state(FNV_D3D9_RENDER_STATE,0);}
static void api_get_render_state(void){device_state(FNV_D3D9_RENDER_STATE,1);}
static void api_set_sampler_state(void){device_state(FNV_D3D9_SAMPLER_STATE,0);}
static void api_get_sampler_state(void){device_state(FNV_D3D9_SAMPLER_STATE,1);}
static void api_set_viewport(void){device_state(FNV_D3D9_VIEWPORT,0);}
static void api_get_viewport(void){device_state(FNV_D3D9_VIEWPORT,1);}
static void api_set_scissor(void){device_state(FNV_D3D9_SCISSOR,0);}
static void api_get_scissor(void){device_state(FNV_D3D9_SCISSOR,1);}
/* Consume the provider's newly acquired public reference. Do not AddRef twice.
 * Implicit backbuffers follow the same public/internal lifetime separation. */
static uint32_t adopt_surface(struct fnv_d3d9_com *c,uintptr_t handle,int *out) {
    uint32_t index;*out=-1;
    if(!handle){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 provider returned no surface",0);}
    for(index=0;index<c->used;index++)if(c->resources[index].handle==handle)break;
    if(index<c->used && c->resources[index].kind!=FNV_D3D9_SURFACE){
        c->config.backend.release(c->config.backend.context,handle);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 surface identity aliases another resource",0);}
    if(index==c->used) {
        for(index=0;index<c->used;index++)if(!c->resources[index].handle)break;
        if(index==c->config.resource_capacity || c->device_references==UINT32_MAX){c->config.backend.release(c->config.backend.context,handle);return FNV_D3D9_OUTOFMEMORY;}
        uint32_t desc[8];uint32_t result=c->config.backend.surface_desc(c->config.backend.context,handle,desc);
        if(result){c->config.backend.release(c->config.backend.context,handle);return result;}
        if(desc[1]!=1 || !desc[6] || !desc[7]){c->config.backend.release(c->config.backend.context,handle);fnv_host_mutex_unlock(&c->guard);invalid("D3D9 invalid surface descriptor",0);}
        c->resources[index]=(struct resource){.handle=handle,.kind=FNV_D3D9_SURFACE};memcpy(c->resources[index].surface_desc,desc,32);
        if(index==c->used)c->used++;
        write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+SURFACE_VTABLE);
    }
    struct resource *r=c->resources+index;
    if(r->references==UINT32_MAX || (!r->references && c->device_references==UINT32_MAX)){
        c->config.backend.release(c->config.backend.context,handle);return FNV_D3D9_OUTOFMEMORY;}
    if(!r->references)c->device_references++;
    r->references++;*out=(int)index;return 0;
}
static void create_surface(int depth) {
    uint32_t self=fnv_cpu_argument_u32(0),width=fnv_cpu_argument_u32(1),height=fnv_cpu_argument_u32(2),format=fnv_cpu_argument_u32(3);
    uint32_t multisample=fnv_cpu_argument_u32(4),quality=fnv_cpu_argument_u32(5),flag=fnv_cpu_argument_u32(6),out_at=fnv_cpu_argument_u32(7),shared=fnv_cpu_argument_u32(8);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1);
    if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,36);return;}
    if(shared)invalid("D3D9 shared surface handles pending",shared);
    if(!c->config.backend.create_surface || !c->config.backend.surface_desc)invalid("D3D9 surface provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);write32(out,0);uintptr_t handle=0;
    uint32_t result=c->config.backend.create_surface(c->config.backend.context,depth,width,height,format,multisample,quality,flag,&handle);int index=-1;
    if(!result)result=adopt_surface(c,handle,&index);
    if(!result)write32(out,resource_address(c,(uint32_t)index));
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,36);
}
static void api_create_color_surface(void){create_surface(0);}
static void api_create_depth_surface(void){create_surface(1);}
static void api_surface_desc(void) {
    uint32_t self=fnv_cpu_argument_u32(0),out_at=fnv_cpu_argument_u32(1);struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,32,1);
    if(!out){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,8);return;}
    fnv_host_mutex_lock(&c->guard);int index=resource_kind(c,self,FNV_D3D9_SURFACE);
    if(index<0){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetDesc requires surface",self);}
    for(unsigned i=0;i<8;i++)write32(out+i*4,c->resources[index].surface_desc[i]);
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(0,8);
}
static void target_binding(int depth,int get) {
    uint32_t self=fnv_cpu_argument_u32(0),slot=depth?0:fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(depth?1:2),argc=depth?2:3;
    struct fnv_d3d9_com *c=module();unsigned char *out=get?range(c,at,4,1):NULL;
    if(slot>=4 || (get && !out)){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,argc*4);return;}
    if((get && (!c->config.backend.get_target || !c->config.backend.surface_desc)) || (!get && !c->config.backend.set_target))invalid("D3D9 target provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);int *binding=depth?&c->depth_target:c->targets+slot;uint32_t result;
    if(get) {
        write32(out,0);uintptr_t handle=0;int index=-1;result=c->config.backend.get_target(c->config.backend.context,depth,slot,&handle);
        if(!result)result=adopt_surface(c,handle,&index);
        if(!result){change_binding(c,binding,index);write32(out,resource_address(c,(uint32_t)index));}
    } else {
        int index=at?resource_kind(c,at,FNV_D3D9_SURFACE):-1;
        if(at && index<0)result=FNV_D3D9_INVALIDCALL;
        else {result=c->config.backend.set_target(c->config.backend.context,depth,slot,index<0?0:c->resources[index].handle);
            if(!result)change_binding(c,binding,index);}
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,argc*4);
}
static void api_set_render_target(void){target_binding(0,0);}
static void api_get_render_target(void){target_binding(0,1);}
static void api_set_depth_target(void){target_binding(1,0);}
static void api_get_depth_target(void){target_binding(1,1);}
static void api_clear(void) {
    uint32_t self=fnv_cpu_argument_u32(0),count=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3);
    uint32_t color=fnv_cpu_argument_u32(4),z=fnv_cpu_argument_u32(5),stencil=fnv_cpu_argument_u32(6);struct fnv_d3d9_com *c=module();
    if(count>1024)invalid("D3D9 Clear rectangle capacity pending",count);
    unsigned char *input=count?range(c,at,count*16,0):NULL;
    if(count && !input){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,28);return;}
    if(!c->config.backend.clear)invalid("D3D9 Clear provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    uint32_t *rects=count?malloc(count*16):NULL;
    if(count && !rects){fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(FNV_D3D9_OUTOFMEMORY,28);return;}
    if(rects)for(uint32_t i=0;i<count*4;i++)rects[i]=read32(input+i*4);
    uint32_t result=c->config.backend.clear(c->config.backend.context,count,rects,flags,color,z,stencil);
    fnv_host_mutex_unlock(&c->guard);free(rects);fnv_cpu_return_u32(result,28);
}
static void scene(int begin) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();
    if(!c->config.backend.scene)invalid("D3D9 scene provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);uint32_t result=FNV_D3D9_INVALIDCALL;
    if(c->in_scene!=begin){result=c->config.backend.scene(c->config.backend.context,begin);if(!result)c->in_scene=begin;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,4);
}
static void api_begin_scene(void){scene(1);}
static void api_end_scene(void){scene(0);}
static void draw(int indexed) {
    uint32_t self=fnv_cpu_argument_u32(0),primitive=fnv_cpu_argument_u32(1),base=0,min_vertex=0,vertices=0,start,primitives;
    if(indexed){base=fnv_cpu_argument_u32(2);min_vertex=fnv_cpu_argument_u32(3);vertices=fnv_cpu_argument_u32(4);start=fnv_cpu_argument_u32(5);primitives=fnv_cpu_argument_u32(6);}
    else {start=fnv_cpu_argument_u32(2);primitives=fnv_cpu_argument_u32(3);}
    struct fnv_d3d9_com *c=module();
    if(!c->config.backend.draw || !c->config.backend.upload_constants)invalid("D3D9 draw/constants provider pending",self);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);int locked=0;
    for(unsigned i=0;i<16;i++)if(c->streams[i].index>=0 && c->resources[c->streams[i].index].locked)locked=1;
    for(unsigned i=0;i<16;i++)if(c->textures[i]>=0){struct resource *r=c->resources+c->textures[i];
        for(uint32_t level=0;level<r->levels;level++)if(r->mips[level].locked)locked=1;}
    if(indexed && c->indices>=0 && c->resources[c->indices].locked)locked=1;
    uint32_t result=FNV_D3D9_INVALIDCALL;
    if(c->in_scene && !locked){result=fnv_d3d9_constants_flush(c->constants,c->config.backend.upload_constants,c->config.backend.context);
        if(!result)result=c->config.backend.draw(c->config.backend.context,indexed,primitive,base,min_vertex,vertices,start,primitives);}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,indexed?28:16);
}
static void api_draw_primitive(void){draw(0);}
static void api_draw_indexed(void){draw(1);}
static void constant(enum fnv_d3d9_stage stage,enum fnv_d3d9_constant_kind kind,int get) {
    uint32_t self=fnv_cpu_argument_u32(0),first=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2),count=fnv_cpu_argument_u32(3);
    struct fnv_d3d9_com *c=module();uint32_t width=kind==FNV_D3D9_BOOLEAN?4:16;
    if(count>256){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,16);return;}
    void *data=count?range(c,at,count*width,get):NULL;
    if(count && !data){fnv_cpu_return_u32(FNV_D3D9_INVALIDCALL,16);return;}
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    uint32_t result=get?fnv_d3d9_constants_get(c->constants,stage,kind,first,count,data):fnv_d3d9_constants_set(c->constants,stage,kind,first,count,data);
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,16);
}
#define CONSTANTS(prefix,stage) \
static void prefix##_sf(void){constant(stage,FNV_D3D9_FLOAT,0);} \
static void prefix##_gf(void){constant(stage,FNV_D3D9_FLOAT,1);} \
static void prefix##_si(void){constant(stage,FNV_D3D9_INTEGER,0);} \
static void prefix##_gi(void){constant(stage,FNV_D3D9_INTEGER,1);} \
static void prefix##_sb(void){constant(stage,FNV_D3D9_BOOLEAN,0);} \
static void prefix##_gb(void){constant(stage,FNV_D3D9_BOOLEAN,1);}
CONSTANTS(vertex,FNV_D3D9_VERTEX)
CONSTANTS(pixel,FNV_D3D9_PIXEL)
static void unsupported(void){invalid("D3D9 COM method pending",fnv_cpu_argument_u32(0));}
#include "d3d9_factory.inc"
static const fnv_cpu_function functions[METHODS]={api_query,api_addref,api_release,api_get_device,api_get_function,
    api_create_vertex,api_create_pixel,vertex_sf,vertex_gf,vertex_si,vertex_gi,vertex_sb,vertex_gb,
    pixel_sf,pixel_gf,pixel_si,pixel_gi,pixel_sb,pixel_gb,unsupported,
    api_set_vs,api_get_vs,api_set_ps,api_get_ps,api_create_vb,api_create_ib,
    api_buffer_type,api_buffer_lock,api_buffer_unlock,api_buffer_desc,api_set_stream,api_get_stream,
    api_set_indices,api_get_indices,api_set_frequency,api_get_frequency,
    api_create_declaration,api_set_declaration,api_get_declaration,api_declaration_elements,
    api_create_texture,api_texture_levels,api_texture_desc,api_texture_lock,api_texture_unlock,api_set_texture,api_get_texture,
    api_set_render_state,api_get_render_state,api_set_sampler_state,api_get_sampler_state,
    api_set_viewport,api_get_viewport,api_set_scissor,api_get_scissor,
    api_create_color_surface,api_create_depth_surface,api_surface_desc,api_set_render_target,api_get_render_target,
    api_set_depth_target,api_get_depth_target,api_clear,
    api_begin_scene,api_end_scene,api_draw_primitive,api_draw_indexed,
    api_direct3d9,api_adapter_count,api_mode_count,api_enum_modes,api_display_mode,api_check_type,api_check_format,
    api_factory_caps,api_create_device,api_device_parent,api_creation_parameters,
    api_unavailable2,api_unavailable4,api_unavailable5,api_unavailable6,api_unavailable7,api_adapter_monitor,api_device_caps};
static void dispose(struct fnv_d3d9_com *c) {
    if(c->catalog)for(size_t i=0;i<c->config.catalog_count;i++)free(c->catalog[i].bytes);
    free(c->catalog);free(c->resources);free(c->free_spans);fnv_d3d9_constants_destroy(c->constants);fnv_host_mutex_destroy(&c->guard);free(c);
}
enum fnv_cpu_result fnv_d3d9_com_create(struct fnv_cpu_runtime *cpu,const struct fnv_d3d9_com_config *cfg,struct fnv_d3d9_com **out) {
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(active_com)return FNV_CPU_BUSY;
    if(!cpu || !cfg || (cfg->objects_address&3) || !cfg->resource_capacity || cfg->resource_capacity>8192 ||
       !cfg->vertex_floats || cfg->vertex_floats>256 || !cfg->pixel_floats || cfg->pixel_floats>224 ||
       cfg->objects_bytes<OBJECT_OFFSET+(uint64_t)cfg->resource_capacity*4 || cfg->catalog_count>8192 ||
       (cfg->catalog_count && !cfg->catalog) || !cfg->backend.create || !cfg->backend.addref || !cfg->backend.release ||
       !cfg->backend.device_addref || !cfg->backend.device_release)return FNV_CPU_INVALID;
    if((!cfg->buffer_address)!=(!cfg->buffer_bytes) || (cfg->buffer_address&15) || (cfg->buffer_bytes&15) ||
       (cfg->buffer_bytes && (uint64_t)cfg->buffer_address<(uint64_t)cfg->objects_address+cfg->objects_bytes &&
        (uint64_t)cfg->buffer_address+cfg->buffer_bytes>cfg->objects_address))return FNV_CPU_INVALID;
    void *objects;enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,cfg->objects_address,cfg->objects_bytes,&objects);if(r)return r;
    if(cfg->memory){unsigned char *backing;uint32_t base,bytes;void *mapped;
        if(!fnv_vm_binding(cfg->memory,&backing,&base,&bytes) ||
           fnv_cpu_prepare_memory(cpu,base,bytes,&mapped)!=FNV_CPU_OK || mapped!=backing ||
           !fnv_vm_access(cfg->memory,cfg->objects_address,cfg->objects_bytes,1) ||
           (cfg->buffer_bytes && !fnv_vm_access(cfg->memory,cfg->buffer_address,cfg->buffer_bytes,1)))return FNV_CPU_INVALID;}
    void *buffer_memory=NULL;if(cfg->buffer_bytes){r=fnv_cpu_prepare_memory(cpu,cfg->buffer_address,cfg->buffer_bytes,&buffer_memory);if(r)return r;}
    struct fnv_d3d9_com *c=calloc(1,sizeof(*c));if(!c)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&c->guard)){free(c);return FNV_CPU_NO_MEMORY;}
    c->config=*cfg;if(cfg->factory){c->factory=*cfg->factory;c->config.factory=&c->factory;
        if(!c->factory.width || !c->factory.height || c->factory.color_format!=21 || (c->factory.depth_format && c->factory.depth_format!=75) || !factory_prepared(cfg,&c->factory)) {dispose(c);return FNV_CPU_INVALID;}
        c->factory_refs=1;}
    c->cpu=cpu;c->objects=objects;c->buffer_memory=buffer_memory;
    c->free_spans=calloc((size_t)cfg->resource_capacity+2,sizeof(*c->free_spans));
    if(cfg->buffer_bytes && c->free_spans){c->free_spans[0]=(struct span){0,cfg->buffer_bytes};c->free_count=1;}
    c->shaders[0]=c->shaders[1]=c->indices=c->declaration=-1;for(unsigned i=0;i<16;i++){c->streams[i]=(struct stream){-1,0,0,1};c->textures[i]=-1;}
    c->depth_target=-1;for(unsigned i=0;i<4;i++)c->targets[i]=-1;
    c->catalog=calloc(cfg->catalog_count?cfg->catalog_count:1,sizeof(*c->catalog));
    c->resources=calloc(cfg->resource_capacity,sizeof(*c->resources));
    if(!c->catalog || !c->resources || !c->free_spans || fnv_d3d9_constants_create(cfg->vertex_floats,cfg->pixel_floats,&c->constants)) {dispose(c);return FNV_CPU_NO_MEMORY;}
    uint64_t total=0;
    for(size_t i=0;i<cfg->catalog_count;i++) {
        const struct fnv_d3d9_shader_code *s=cfg->catalog+i;
        if(!s->bytes || s->size<8 || s->size>1048576 || (s->size&3) || (total+=s->size)>64u*1024*1024){dispose(c);return FNV_CPU_INVALID;}
        const unsigned char *bytes=s->bytes;uint32_t version=read32(bytes),stage=version>>16;
        if((stage!=0xFFFE && stage!=0xFFFF) || read32(bytes+s->size-4)!=0xFFFF){dispose(c);return FNV_CPU_INVALID;}
        c->catalog[i].bytes=malloc(s->size);if(!c->catalog[i].bytes){dispose(c);return FNV_CPU_NO_MEMORY;}
        memcpy(c->catalog[i].bytes,s->bytes,s->size);c->catalog[i].size=s->size;c->catalog[i].stage=stage==0xFFFE?FNV_D3D9_VERTEX:FNV_D3D9_PIXEL;
    }
    if(cfg->memory && !fnv_vm_retain(cfg->memory)){dispose(c);return FNV_CPU_INVALID;}
    r=fnv_cpu_register_native(cpu,functions,METHODS,c->tokens);
    if(r){fnv_vm_release_reference(cfg->memory);dispose(c);return r;}
    memset(c->objects,0,cfg->objects_bytes);
    for(unsigned i=0;i<DEVICE_SLOTS;i++)write32(c->objects+i*4,c->tokens[19]);
    for(unsigned i=0;i<3;i++){write32(c->objects+i*4,c->tokens[i]);write32(c->objects+DEVICE_SLOTS*4+i*4,c->tokens[i]);}
    write32(c->objects+DEVICE_SLOTS*4+12,c->tokens[3]);write32(c->objects+DEVICE_SLOTS*4+16,c->tokens[4]);
    write32(c->objects+91*4,c->tokens[5]);write32(c->objects+106*4,c->tokens[6]);
    for(unsigned i=0;i<6;i++){write32(c->objects+(94+i)*4,c->tokens[7+i]);write32(c->objects+(109+i)*4,c->tokens[13+i]);}
    for(unsigned i=0;i<14;i++)write32(c->objects+BUFFER_VTABLE+i*4,c->tokens[19]);
    for(unsigned i=0;i<4;i++)write32(c->objects+BUFFER_VTABLE+i*4,c->tokens[i]);
    for(unsigned i=0;i<4;i++)write32(c->objects+BUFFER_VTABLE+(10+i)*4,c->tokens[26+i]);
    write32(c->objects+92*4,c->tokens[20]);write32(c->objects+93*4,c->tokens[21]);
    write32(c->objects+107*4,c->tokens[22]);write32(c->objects+108*4,c->tokens[23]);
    write32(c->objects+26*4,c->tokens[24]);write32(c->objects+27*4,c->tokens[25]);
    write32(c->objects+100*4,c->tokens[30]);write32(c->objects+101*4,c->tokens[31]);
    write32(c->objects+104*4,c->tokens[32]);write32(c->objects+105*4,c->tokens[33]);
    write32(c->objects+102*4,c->tokens[34]);write32(c->objects+103*4,c->tokens[35]);
    for(unsigned i=0;i<4;i++)write32(c->objects+DECL_VTABLE+i*4,c->tokens[i]);
    write32(c->objects+DECL_VTABLE+16,c->tokens[39]);
    write32(c->objects+86*4,c->tokens[36]);write32(c->objects+87*4,c->tokens[37]);write32(c->objects+88*4,c->tokens[38]);
    for(unsigned i=0;i<22;i++)write32(c->objects+TEXTURE_VTABLE+i*4,c->tokens[19]);
    for(unsigned i=0;i<4;i++)write32(c->objects+TEXTURE_VTABLE+i*4,c->tokens[i]);
    write32(c->objects+TEXTURE_VTABLE+10*4,c->tokens[26]);write32(c->objects+TEXTURE_VTABLE+13*4,c->tokens[41]);
    write32(c->objects+TEXTURE_VTABLE+17*4,c->tokens[42]);write32(c->objects+TEXTURE_VTABLE+19*4,c->tokens[43]);
    write32(c->objects+TEXTURE_VTABLE+20*4,c->tokens[44]);
    write32(c->objects+23*4,c->tokens[40]);write32(c->objects+65*4,c->tokens[45]);write32(c->objects+64*4,c->tokens[46]);
    const unsigned state_slots[8]={57,58,69,68,47,48,75,76};
    for(unsigned i=0;i<8;i++)write32(c->objects+state_slots[i]*4,c->tokens[47+i]);
    for(unsigned i=0;i<17;i++)write32(c->objects+SURFACE_VTABLE+i*4,c->tokens[19]);
    for(unsigned i=0;i<4;i++)write32(c->objects+SURFACE_VTABLE+i*4,c->tokens[i]);
    write32(c->objects+SURFACE_VTABLE+10*4,c->tokens[26]);write32(c->objects+SURFACE_VTABLE+12*4,c->tokens[57]);
    const unsigned surface_slots[8]={28,29,0,37,38,39,40,43};
    for(unsigned i=0;i<8;i++)if(i!=2)write32(c->objects+surface_slots[i]*4,c->tokens[55+i]);
    const unsigned draw_slots[4]={41,42,81,82};
    for(unsigned i=0;i<4;i++)write32(c->objects+draw_slots[i]*4,c->tokens[63+i]);
    if(cfg->factory){
        const unsigned map[17]={0,1,2,78,68,79,69,70,71,72,73,82,81,80,74,83,75};
        for(unsigned i=0;i<17;++i)write32(c->objects+FACTORY_VTABLE+i*4,c->tokens[map[i]]);
        write32(c->objects+FACTORY_OBJECT,cfg->objects_address+FACTORY_VTABLE);
        write32(c->objects+7*4,c->tokens[84]);write32(c->objects+6*4,c->tokens[76]);write32(c->objects+9*4,c->tokens[77]);
    }
    write32(c->objects+OBJECT_OFFSET-4,cfg->objects_address);c->device_references=1;active_com=c;*out=c;return FNV_CPU_OK;
}
const struct fnv_cpu_export *fnv_d3d9_com_exports(const struct fnv_d3d9_com *c,size_t *count){
    static const struct fnv_cpu_export exports[1]={{"d3d9.dll","Direct3DCreate9",0,0,api_direct3d9}};
    if(count)*count=c && c==active_com && c->config.factory?1:0;
    return c && c==active_com && c->config.factory?exports:NULL;
}
uint32_t fnv_d3d9_com_device(const struct fnv_d3d9_com *c){return c?device_address(c):0;}
uint32_t fnv_d3d9_com_flush(struct fnv_d3d9_com *c,fnv_d3d9_constants_upload upload,void *ctx){
    if(!c)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&c->guard);uint32_t result=fnv_d3d9_constants_flush(c->constants,upload,ctx);
    fnv_host_mutex_unlock(&c->guard);return result;
}
enum fnv_cpu_result fnv_d3d9_com_destroy(struct fnv_d3d9_com *c) {
    if(!c || c!=active_com)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(c->cpu,c->tokens[0],METHODS);if(r)return r;
    for(uint32_t i=0;i<c->used;i++)while(c->resources[i].references)decrement(c,(int)i);
    while(c->device_references)decrement(c,-1);
    memset(c->objects,0,c->config.objects_bytes);fnv_vm_release_reference(c->config.memory);active_com=NULL;dispose(c);return FNV_CPU_OK;
}
