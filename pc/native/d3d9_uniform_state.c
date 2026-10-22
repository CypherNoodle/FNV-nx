#include "d3d9_uniform_state.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>
struct bank { uint32_t f[256*4],i[16*4],b[16],floats; };
struct binding { struct fnv_uniform_plan plan;struct fnv_uniform_entry entries[512];int bound; };
struct version { uint64_t ticket,serial;uint32_t phase; };
struct fnv_uniform_state {
    fnv_host_mutex lock;
    struct bank banks[2];struct binding bindings[2];struct version versions[64];
    struct fnv_uniform_stats stats;
    uint32_t slots,max_bytes,next_slot;
    uint64_t next_ticket,last_submit;
    unsigned char *storage;
};
static struct fnv_uniform_banks view(struct bank *b){
    struct fnv_uniform_banks result={{b->f,b->i,b->b},{b->floats,16,16}};return result;
}
uint32_t fnv_uniform_state_create(const struct fnv_uniform_config *c,struct fnv_uniform_state **out){
    if(!c || !out || c->vertex_floats>256 || c->pixel_floats>256 || !c->slots || c->slots>64 ||
       !c->max_bytes || c->max_bytes>65536 || c->max_bytes%16)return FNV_D3D9_INVALIDCALL;
    struct fnv_uniform_state *s=calloc(1,sizeof(*s));if(!s)return FNV_D3D9_OUTOFMEMORY;
    s->storage=calloc((size_t)c->slots*2+1,c->max_bytes);
    if(!s->storage){free(s);return FNV_D3D9_OUTOFMEMORY;}
    if(!fnv_host_mutex_init(&s->lock)){free(s->storage);free(s);return FNV_D3D9_OUTOFMEMORY;}
    s->slots=c->slots;s->max_bytes=c->max_bytes;s->banks[0].floats=c->vertex_floats;s->banks[1].floats=c->pixel_floats;
    *out=s;return 0;
}
uint32_t fnv_uniform_state_destroy(struct fnv_uniform_state *s){
    if(!s)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);
    for(uint32_t i=0;i<s->slots;++i)if(s->versions[i].phase){fnv_host_mutex_unlock(&s->lock);return FNV_UNIFORM_BUSY;}
    fnv_host_mutex_unlock(&s->lock);fnv_host_mutex_destroy(&s->lock);free(s->storage);free(s);return 0;
}
int fnv_uniform_state_upload(void *opaque,enum fnv_d3d9_stage stage,enum fnv_d3d9_constant_kind kind,
    uint32_t first,uint32_t count,const uint32_t *bits){
    struct fnv_uniform_state *s=opaque;
    if(!s || (unsigned)stage>1 || (unsigned)kind>2 || (count && !bits))return 0;
    fnv_host_mutex_lock(&s->lock);
    struct fnv_uniform_banks b=view(&s->banks[stage]);uint32_t limit=b.count[kind],width=kind==2?1:4;
    if(first>limit || count>limit-first){fnv_host_mutex_unlock(&s->lock);return 0;}
    if(count){memmove((uint32_t*)b.bits[kind]+first*width,bits,(size_t)count*width*4);s->stats.uploads++;}
    fnv_host_mutex_unlock(&s->lock);return 1;
}
uint32_t fnv_uniform_state_bind_batch(struct fnv_uniform_state *s,uint32_t mask,const struct fnv_uniform_plan *const plans[2],int validate_only){
    if(!s || mask>3 || (mask && !plans))return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);uint32_t result=0;
    for(unsigned stage=0;stage<2;++stage)if((mask&(1u<<stage)) && plans[stage]){
        struct fnv_uniform_banks bank=view(&s->banks[stage]);
        result=fnv_d3d9_uniform_pack(plans[stage],&bank,s->storage+(size_t)s->slots*2*s->max_bytes,s->max_bytes);if(result)goto done;
    }
    if(!validate_only)for(unsigned stage=0;stage<2;++stage)if(mask&(1u<<stage)){
        struct binding *b=&s->bindings[stage];const struct fnv_uniform_plan *plan=plans[stage];
        if(!plan){b->bound=0;continue;}
        b->plan=*plan;if(plan->count)memmove(b->entries,plan->entries,(size_t)plan->count*sizeof(*b->entries));b->plan.entries=b->entries;b->bound=1;
    }
done:fnv_host_mutex_unlock(&s->lock);return result;
}
uint32_t fnv_uniform_state_bind(struct fnv_uniform_state *s,enum fnv_d3d9_stage stage,const struct fnv_uniform_plan *plan){
    if((unsigned)stage>1)return FNV_D3D9_INVALIDCALL;
    const struct fnv_uniform_plan *plans[2]={0};plans[stage]=plan;return fnv_uniform_state_bind_batch(s,1u<<stage,plans,0);
}
static uint32_t prepare_draw(struct fnv_uniform_state *s,const void *vertex,uint32_t bytes,int empty_pixel,struct fnv_uniform_draw *out){
    if(!s || !out || (vertex && (!bytes || bytes>s->max_bytes)))return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);
    if((!vertex && !s->bindings[0].bound) || (!empty_pixel && !s->bindings[1].bound)){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_INVALIDCALL;}
    uint32_t index=s->slots;
    for(uint32_t i=0;i<s->slots;++i){uint32_t at=(s->next_slot+i)%s->slots;if(!s->versions[at].phase){index=at;break;}}
    if(index==s->slots){fnv_host_mutex_unlock(&s->lock);return FNV_UNIFORM_BUSY;}
    if(s->next_ticket==UINT64_MAX){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_OUTOFMEMORY;}
    struct fnv_uniform_draw draw={0};
    for(unsigned stage=0;stage<2;++stage){
        unsigned char *data=s->storage+((size_t)index*2+stage)*s->max_bytes;
        if(!stage && vertex){memcpy(data,vertex,bytes);draw.bytes[0]=bytes;draw.data[0]=data;continue;}
        if(stage==1 && empty_pixel)continue;
        struct fnv_uniform_banks b=view(&s->banks[stage]);
        uint32_t r=fnv_d3d9_uniform_pack(&s->bindings[stage].plan,&b,data,s->max_bytes);
        if(r){fnv_host_mutex_unlock(&s->lock);return r;}
        draw.bytes[stage]=s->bindings[stage].plan.bytes;draw.data[stage]=draw.bytes[stage]?data:NULL;
    }
    draw.ticket=++s->next_ticket;s->versions[index]=(struct version){draw.ticket,0,1};
    s->next_slot=(index+1)%s->slots;s->stats.prepared++;*out=draw;
    fnv_host_mutex_unlock(&s->lock);return 0;
}
uint32_t fnv_uniform_state_prepare(struct fnv_uniform_state *s,struct fnv_uniform_draw *out){return prepare_draw(s,NULL,0,0,out);}
uint32_t fnv_uniform_state_prepare_vertex(struct fnv_uniform_state *s,const void *data,uint32_t bytes,struct fnv_uniform_draw *out){
    if(!data)return FNV_D3D9_INVALIDCALL;
    return prepare_draw(s,data,bytes,0,out);
}
uint32_t fnv_uniform_state_prepare_fixed(struct fnv_uniform_state *s,const void *data,uint32_t bytes,struct fnv_uniform_draw *out){
    if(!data)return FNV_D3D9_INVALIDCALL;
    return prepare_draw(s,data,bytes,1,out);
}
static struct version *find(struct fnv_uniform_state *s,uint64_t ticket){
    if(ticket)for(uint32_t i=0;i<s->slots;++i)if(s->versions[i].phase && s->versions[i].ticket==ticket)return s->versions+i;
    return NULL;
}
uint32_t fnv_uniform_state_commit(struct fnv_uniform_state *s,uint64_t ticket,uint64_t serial){
    return fnv_uniform_state_commit_batch(s,&ticket,1,serial);
}
uint32_t fnv_uniform_state_commit_batch(struct fnv_uniform_state *s,const uint64_t *tickets,uint32_t count,uint64_t serial){
    if(!s || !tickets || !count || count>64)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);struct version *selected[64];
    if(serial<=s->stats.completed_serial || serial<=s->last_submit){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_INVALIDCALL;}
    for(uint32_t i=0;i<count;++i){
        selected[i]=find(s,tickets[i]);
        if(!selected[i] || selected[i]->phase!=1){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_INVALIDCALL;}
        for(uint32_t j=0;j<i;++j)if(selected[i]==selected[j]){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_INVALIDCALL;}
    }
    for(uint32_t i=0;i<count;++i){selected[i]->phase=2;selected[i]->serial=serial;}
    s->last_submit=serial;s->stats.committed+=count;
    fnv_host_mutex_unlock(&s->lock);return 0;
}
uint32_t fnv_uniform_state_cancel(struct fnv_uniform_state *s,uint64_t ticket){
    if(!s)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);struct version *v=find(s,ticket);
    uint32_t r=FNV_D3D9_INVALIDCALL;if(v && v->phase==1){memset(v,0,sizeof(*v));r=0;}
    fnv_host_mutex_unlock(&s->lock);return r;
}
uint32_t fnv_uniform_state_complete(struct fnv_uniform_state *s,uint64_t serial){
    if(!s)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);
    if(!serial || serial<s->stats.completed_serial || serial>s->last_submit){fnv_host_mutex_unlock(&s->lock);return FNV_D3D9_INVALIDCALL;}
    for(uint32_t i=0;i<s->slots;++i){struct version *v=s->versions+i;if(v->phase==2 && v->serial<=serial){memset(v,0,sizeof(*v));s->stats.retired++;}}
    s->stats.completed_serial=serial;fnv_host_mutex_unlock(&s->lock);return 0;
}
uint32_t fnv_uniform_state_stats(struct fnv_uniform_state *s,struct fnv_uniform_stats *out){
    if(!s || !out)return FNV_D3D9_INVALIDCALL;
    fnv_host_mutex_lock(&s->lock);struct fnv_uniform_stats copy=s->stats;
    for(uint32_t i=0;i<s->slots;++i){unsigned phase=s->versions[i].phase;if(!phase)copy.free_slots++;else if(phase==1)copy.reserved_slots++;else copy.submitted_slots++;}
    *out=copy;fnv_host_mutex_unlock(&s->lock);return 0;
}
