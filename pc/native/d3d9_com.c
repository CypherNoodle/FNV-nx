#include "fnv_sdk_runtime.h"
#include "d3d9_com.h"
#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>

enum { DEVICE_SLOTS=119,METHODS=20,OBJECT_OFFSET=512 };
enum { E_POINTER_VALUE=0x80004003u,E_NOINTERFACE_VALUE=0x80004002u };
struct code { unsigned char *bytes;uint32_t size;enum fnv_d3d9_stage stage; };
struct shader { uintptr_t handle;uint32_t references;struct code *code; };
struct fnv_d3d9_com {
    struct fnv_cpu_runtime *cpu;
    struct fnv_d3d9_com_config config;
    struct fnv_d3d9_constants *constants;
    struct code *catalog;
    struct shader *shaders;
    unsigned char *objects;
    uint32_t tokens[METHODS],used,device_references;
    fnv_host_mutex guard;
};
static struct fnv_d3d9_com *active_com;
static const unsigned char iid_unknown[16]={0,0,0,0,0,0,0,0,0xC0,0,0,0,0,0,0,0x46};
static const unsigned char iid_device[16]={0x96,0x3B,0x22,0xD0,0x7A,0xBF,0xFD,0x43,0x92,0xBD,0xA4,0x3B,0x0D,0x82,0xB9,0xEB};
static const unsigned char iid_vertex[16]={0x7E,0x55,0xC5,0xEF,0x65,0x62,0x13,0x46,0x8A,0x94,0x43,0x85,0x78,0x89,0xEB,0x36};
static const unsigned char iid_pixel[16]={0xDC,0xDB,0x3B,0x6D,0x02,0x5B,0x15,0x44,0xB8,0x52,0xCE,0x5E,0x8B,0xCC,0xB2,0x89};
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
static uint32_t shader_address(const struct fnv_d3d9_com *c,uint32_t index){return c->config.objects_address+OBJECT_OFFSET+index*4;}
/* Return -1 for device, otherwise a live shader index. Failure unlocks before
 * unwinding to the CPU boundary. Guest pointer probes happen before locking. */
static int object(struct fnv_d3d9_com *c,uint32_t at,int device_only) {
    if(at==device_address(c) && c->device_references && read32(c->objects+OBJECT_OFFSET-4)==c->config.objects_address)return -1;
    if(!device_only && at>=shader_address(c,0) && !((at-shader_address(c,0))&3)) {
        uint32_t index=(at-shader_address(c,0))/4;
        if(index<c->used && c->shaders[index].references &&
           read32(c->objects+OBJECT_OFFSET+index*4)==c->config.objects_address+DEVICE_SLOTS*4)return (int)index;
    }
    fnv_host_mutex_unlock(&c->guard);invalid("D3D9 invalid/stale this pointer",at);
}
static void increment(struct fnv_d3d9_com *c,int index) {
    if(index<0){c->device_references++;c->config.backend.device_addref(c->config.backend.context);}
    else {c->shaders[index].references++;c->config.backend.addref(c->config.backend.context,c->shaders[index].handle);}
}
static uint32_t decrement(struct fnv_d3d9_com *c,int index) {
    if(index<0){c->config.backend.device_release(c->config.backend.context);return --c->device_references;}
    struct shader *s=c->shaders+index;c->config.backend.release(c->config.backend.context,s->handle);
    if(!--s->references){s->handle=0;c->device_references--;}
    return s->references;
}
static void api_query(void) {
    uint32_t self=fnv_cpu_argument_u32(0),iid_at=fnv_cpu_argument_u32(1),out_at=fnv_cpu_argument_u32(2);
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,out_at,4,1),*iid=range(c,iid_at,16,0);
    if(!out || !iid){fnv_cpu_return_u32(E_POINTER_VALUE,12);return;}
    unsigned char requested[16];memcpy(requested,iid,16);
    fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    const unsigned char *supported=index<0?iid_device:c->shaders[index].code->stage==FNV_D3D9_VERTEX?iid_vertex:iid_pixel;
    uint32_t result=E_NOINTERFACE_VALUE;write32(out,0);
    if(!memcmp(requested,iid_unknown,16) || !memcmp(requested,supported,16)) {
        uint32_t refs=index<0?c->device_references:c->shaders[index].references;
        if(refs==UINT32_MAX){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 reference overflow",self);}
        increment(c,index);write32(out,self);result=0;
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_addref(void) {
    uint32_t self=fnv_cpu_argument_u32(0);struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);int index=object(c,self,0);
    uint32_t refs=index<0?c->device_references:c->shaders[index].references;
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
    if(index<0){fnv_host_mutex_unlock(&c->guard);invalid("D3D9 GetFunction requires shader",self);}
    struct code *code=c->shaders[index].code;fnv_host_mutex_unlock(&c->guard);
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
        for(index=0;index<c->used;index++)if(c->shaders[index].handle==handle && c->shaders[index].references)break;
        if(index<c->used && (c->shaders[index].code->size!=code->size ||
           memcmp(c->shaders[index].code->bytes,code->bytes,code->size))) {
            c->config.backend.release(c->config.backend.context,handle);fnv_host_mutex_unlock(&c->guard);
            invalid("D3D9 backend reused a different live shader identity",code_at);
        }
        if(index==c->used) {
            for(index=0;index<c->used;index++)if(!c->shaders[index].references)break;
            if((index==c->used && c->used==c->config.shader_capacity) || c->device_references==UINT32_MAX){
                c->config.backend.release(c->config.backend.context,handle);result=FNV_D3D9_OUTOFMEMORY;
            } else {c->shaders[index]=(struct shader){handle,0,code};if(index==c->used)c->used++;c->device_references++;
                write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+DEVICE_SLOTS*4);}
        }
        if(!result) {
            if(c->shaders[index].references==UINT32_MAX){c->config.backend.release(c->config.backend.context,handle);result=FNV_D3D9_OUTOFMEMORY;}
            else {c->shaders[index].references++;write32(out,shader_address(c,index));}
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(result,12);
}
static void api_create_vertex(void){create_shader(FNV_D3D9_VERTEX);}
static void api_create_pixel(void){create_shader(FNV_D3D9_PIXEL);}
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
static const fnv_cpu_function functions[METHODS]={api_query,api_addref,api_release,api_get_device,api_get_function,
    api_create_vertex,api_create_pixel,vertex_sf,vertex_gf,vertex_si,vertex_gi,vertex_sb,vertex_gb,
    pixel_sf,pixel_gf,pixel_si,pixel_gi,pixel_sb,pixel_gb,unsupported};
static void dispose(struct fnv_d3d9_com *c) {
    if(c->catalog)for(size_t i=0;i<c->config.catalog_count;i++)free(c->catalog[i].bytes);
    free(c->catalog);free(c->shaders);fnv_d3d9_constants_destroy(c->constants);fnv_host_mutex_destroy(&c->guard);free(c);
}
enum fnv_cpu_result fnv_d3d9_com_create(struct fnv_cpu_runtime *cpu,const struct fnv_d3d9_com_config *cfg,struct fnv_d3d9_com **out) {
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;if(active_com)return FNV_CPU_BUSY;
    if(!cpu || !cfg || (cfg->objects_address&3) || !cfg->shader_capacity || cfg->shader_capacity>8192 ||
       !cfg->vertex_floats || cfg->vertex_floats>256 || !cfg->pixel_floats || cfg->pixel_floats>224 ||
       cfg->objects_bytes<OBJECT_OFFSET+(uint64_t)cfg->shader_capacity*4 || cfg->catalog_count>8192 ||
       (cfg->catalog_count && !cfg->catalog) || !cfg->backend.create || !cfg->backend.addref || !cfg->backend.release ||
       !cfg->backend.device_addref || !cfg->backend.device_release)return FNV_CPU_INVALID;
    void *objects;enum fnv_cpu_result r=fnv_cpu_prepare_memory(cpu,cfg->objects_address,cfg->objects_bytes,&objects);if(r)return r;
    if(cfg->memory){unsigned char *backing;uint32_t base,bytes;void *mapped;
        if(!fnv_vm_binding(cfg->memory,&backing,&base,&bytes) ||
           fnv_cpu_prepare_memory(cpu,base,bytes,&mapped)!=FNV_CPU_OK || mapped!=backing ||
           !fnv_vm_access(cfg->memory,cfg->objects_address,cfg->objects_bytes,1))return FNV_CPU_INVALID;}
    struct fnv_d3d9_com *c=calloc(1,sizeof(*c));if(!c)return FNV_CPU_NO_MEMORY;
    if(!fnv_host_mutex_init(&c->guard)){free(c);return FNV_CPU_NO_MEMORY;}
    c->config=*cfg;c->cpu=cpu;c->objects=objects;
    c->catalog=calloc(cfg->catalog_count?cfg->catalog_count:1,sizeof(*c->catalog));
    c->shaders=calloc(cfg->shader_capacity,sizeof(*c->shaders));
    if(!c->catalog || !c->shaders || fnv_d3d9_constants_create(cfg->vertex_floats,cfg->pixel_floats,&c->constants)) {dispose(c);return FNV_CPU_NO_MEMORY;}
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
    write32(c->objects+OBJECT_OFFSET-4,cfg->objects_address);c->device_references=1;active_com=c;*out=c;return FNV_CPU_OK;
}
uint32_t fnv_d3d9_com_device(const struct fnv_d3d9_com *c){return c?device_address(c):0;}
uint32_t fnv_d3d9_com_flush(struct fnv_d3d9_com *c,fnv_d3d9_constants_upload upload,void *ctx){return c?fnv_d3d9_constants_flush(c->constants,upload,ctx):FNV_D3D9_INVALIDCALL;}
enum fnv_cpu_result fnv_d3d9_com_destroy(struct fnv_d3d9_com *c) {
    if(!c || c!=active_com)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_cpu_unregister_native(c->cpu,c->tokens[0],METHODS);if(r)return r;
    for(uint32_t i=0;i<c->used;i++)while(c->shaders[i].references)decrement(c,(int)i);
    while(c->device_references)decrement(c,-1);
    memset(c->objects,0,c->config.objects_bytes);fnv_vm_release_reference(c->config.memory);active_com=NULL;dispose(c);return FNV_CPU_OK;
}
