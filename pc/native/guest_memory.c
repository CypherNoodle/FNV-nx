#include "guest_memory.h"
#include "host_mutex.h"
#include <stdlib.h>
#include <string.h>

enum { INVALID=87,ADDRESS=487,NO_MEMORY=8,NOT_SUPPORTED=50,BUSY=170 };
struct fnv_guest_memory {
    struct fnv_vm_config config;
    struct fnv_vm_page *pages;
    uint32_t count,reserved,committed,references,poisoned;
    fnv_host_mutex guard;
};
_Static_assert(sizeof(struct fnv_vm_information)==28,"Guest MBI32 ABI");
static struct fnv_vm_result result(uint32_t value,uint32_t error,uint32_t native) {
    return (struct fnv_vm_result){value,error,native};
}
static void lock(struct fnv_guest_memory *v) { fnv_host_mutex_lock(&v->guard); }
static void unlock(struct fnv_guest_memory *v) { fnv_host_mutex_unlock(&v->guard); }
static int protection_valid(uint32_t p) { return p==1 || p==2 || p==4; }
static int span(const struct fnv_guest_memory *v,uint32_t at,uint32_t bytes,uint32_t alignment,uint32_t *first,uint32_t *count) {
    uint64_t low=(uint64_t)at&~((uint64_t)alignment-1);
    uint64_t high=((uint64_t)at+bytes+4095)&~UINT64_C(4095);
    if(!bytes || low<v->config.base || high>(uint64_t)v->config.base+v->config.bytes || high<=low)return 0;
    *first=(uint32_t)(low-v->config.base)/4096;*count=(uint32_t)(high-low)/4096;return 1;
}
static int contained(const struct fnv_guest_memory *v,uint32_t first,uint32_t count) {
    uint32_t owner=v->pages[first].owner;
    if(!owner)return 0;
    for(uint32_t i=first;i<first+count;i++)if(v->pages[i].owner!=owner)return 0;
    return 1;
}
static struct fnv_vm_result apply(struct fnv_guest_memory *v,enum fnv_vm_operation op,uint32_t first,uint32_t count,uint32_t protection) {
    struct fnv_vm_transition t={op,v->config.backing+(size_t)first*4096,count*4096,protection,v->pages+first};
    struct fnv_vm_backend_result r=v->config.apply(v->config.context,&t);
    if(r.status==FNV_VM_APPLIED)return result(1,0,0);
    if(r.status!=FNV_VM_REJECTED){v->poisoned=1;return result(0,FNV_VM_CORRUPT,r.native_error);}
    return result(0,NO_MEMORY,r.native_error);
}
struct fnv_vm_result fnv_vm_create(const struct fnv_vm_config *c,struct fnv_guest_memory **out) {
    if(!out)return result(0,INVALID,0);
    *out=NULL;
    if(!c || !c->backing || ((uintptr_t)c->backing&4095) || !c->apply || !c->bytes ||
       (c->base&65535) || c->base<65536 || (c->bytes&4095) || (c->commit_limit&4095) ||
       c->commit_limit>c->bytes || (uint64_t)c->base+c->bytes>UINT64_C(0x100000000))return result(0,INVALID,0);
    struct fnv_guest_memory *v=calloc(1,sizeof(*v));if(!v)return result(0,NO_MEMORY,0);
    v->config=*c;v->count=c->bytes/4096;
    if(!fnv_host_mutex_init(&v->guard)){free(v);return result(0,NO_MEMORY,0);}
    v->pages=calloc(v->count,sizeof(*v->pages));if(!v->pages){fnv_host_mutex_destroy(&v->guard);free(v);return result(0,NO_MEMORY,0);}
    struct fnv_vm_result r=apply(v,FNV_VM_INITIALIZE,0,v->count,1);
    if(r.error){free(v->pages);fnv_host_mutex_destroy(&v->guard);free(v);return r;}
    *out=v;return result(1,0,0);
}
struct fnv_vm_result fnv_vm_destroy(struct fnv_guest_memory *v) {
    if(!v)return result(0,INVALID,0);
    lock(v);if(v->references){unlock(v);return result(0,BUSY,0);}
    struct fnv_vm_result r=apply(v,FNV_VM_RESTORE_BACKING,0,v->count,4);
    unlock(v);if(!r.error){free(v->pages);fnv_host_mutex_destroy(&v->guard);free(v);}return r;
}
int fnv_vm_retain(struct fnv_guest_memory *v) {
    if(!v)return 0;
    lock(v);int ok=!v->poisoned && v->references<UINT32_MAX;if(ok)v->references++;unlock(v);return ok;
}
void fnv_vm_release_reference(struct fnv_guest_memory *v) {
    if(v){lock(v);if(v->references)v->references--;unlock(v);}
}
int fnv_vm_binding(const struct fnv_guest_memory *v,unsigned char **backing,uint32_t *base,uint32_t *bytes) {
    if(!v || !backing || !base || !bytes)return 0;
    *backing=v->config.backing;*base=v->config.base;*bytes=v->config.bytes;return 1;
}
struct fnv_vm_result fnv_vm_allocate(struct fnv_guest_memory *v,uint32_t at,uint32_t bytes,uint32_t flags,uint32_t protection) {
    if(!v)return result(0,INVALID,0);
    if(flags!=FNV_VM_COMMIT && flags!=FNV_VM_RESERVE && flags!=(FNV_VM_COMMIT|FNV_VM_RESERVE))return result(0,NOT_SUPPORTED,0);
    if(!protection_valid(protection))return result(0,NOT_SUPPORTED,0);
    if(!bytes)return result(0,INVALID,0);
    lock(v);struct fnv_vm_result r=result(0,ADDRESS,0);
    uint32_t first=0,count=0,needed=0;int reserve=(flags&FNV_VM_RESERVE)||!at;
    if(v->poisoned){r.error=FNV_VM_CORRUPT;goto done;}
    if(reserve && !at) {
        uint64_t n=((uint64_t)bytes+4095)/4096;
        if(n>v->count){r.error=NO_MEMORY;goto done;}count=(uint32_t)n;
        for(;;) {
            if(first>v->count || count>v->count-first){r.error=NO_MEMORY;goto done;}
            uint32_t i;
            for(i=first;i<first+count;i++)if(v->pages[i].owner)break;
            if(i==first+count)break;
            uint32_t owner=v->pages[i].owner-1;
            first=(owner+v->pages[owner].allocation_pages+15)&~15u;
        }
    } else if(!span(v,at,bytes,reserve?65536:4096,&first,&count))goto done;
    if(reserve) {for(uint32_t i=first;i<first+count;i++)if(v->pages[i].owner)goto done;}
    else if(!contained(v,first,count))goto done;
    if(flags&FNV_VM_COMMIT) {
        for(uint32_t i=first;i<first+count;i++)if(!v->pages[i].protection)needed++;
        if(needed>(v->config.commit_limit/4096)-v->committed){r.error=NO_MEMORY;goto done;}
        r=apply(v,FNV_VM_COMMIT_PAGES,first,count,protection);if(r.error)goto done;
    }
    if(reserve) {
        for(uint32_t i=first;i<first+count;i++)v->pages[i].owner=first+1;
        v->pages[first].allocation_pages=count;v->pages[first].allocation_protect=protection;v->reserved+=count;
    }
    if(flags&FNV_VM_COMMIT) {
        for(uint32_t i=first;i<first+count;i++)v->pages[i].protection=protection;
        v->committed+=needed;
    }
    r=result(v->config.base+first*4096,0,0);
done:unlock(v);return r;
}
struct fnv_vm_result fnv_vm_free(struct fnv_guest_memory *v,uint32_t at,uint32_t bytes,uint32_t flags) {
    if(!v)return result(0,INVALID,0);
    if(flags!=FNV_VM_DECOMMIT && flags!=FNV_VM_RELEASE)return result(0,NOT_SUPPORTED,0);
    if(flags==FNV_VM_RELEASE && bytes)return result(0,INVALID,0);
    lock(v);struct fnv_vm_result r=result(0,ADDRESS,0);uint32_t first,count;
    if(v->poisoned){r.error=FNV_VM_CORRUPT;goto done;}
    if(!bytes || flags==FNV_VM_RELEASE) {
        if(at<v->config.base || (at&4095) || (uint64_t)at>=(uint64_t)v->config.base+v->config.bytes)goto done;
        first=(at-v->config.base)/4096;
        if(v->pages[first].owner!=first+1)goto done;
        count=v->pages[first].allocation_pages;
    } else if(!span(v,at,bytes,4096,&first,&count) || !contained(v,first,count))goto done;
    r=apply(v,FNV_VM_HIDE_PAGES,first,count,1);if(r.error)goto done;
    for(uint32_t i=first;i<first+count;i++)if(v->pages[i].protection){v->pages[i].protection=0;v->committed--;}
    if(flags==FNV_VM_RELEASE){memset(v->pages+first,0,(size_t)count*sizeof(*v->pages));v->reserved-=count;}
    r=result(1,0,0);
done:unlock(v);return r;
}
struct fnv_vm_result fnv_vm_query(struct fnv_guest_memory *v,uint32_t at,struct fnv_vm_information *out) {
    if(!v || !out)return result(0,INVALID,0);
    lock(v);struct fnv_vm_result r=result(0,INVALID,0);
    if(v->poisoned){r.error=FNV_VM_CORRUPT;goto done;}
    uint32_t first,count;
    if(!span(v,at,1,4096,&first,&count))goto done;
    struct fnv_vm_page p=v->pages[first];count=1;
    while(first+count<v->count && v->pages[first+count].owner==p.owner &&
          v->pages[first+count].protection==p.protection)count++;
    struct fnv_vm_information info={v->config.base+first*4096,0,0,count*4096,FNV_VM_FREE,0,0};
    if(p.owner){info.allocation_base=v->config.base+(p.owner-1)*4096;
        info.allocation_protect=v->pages[p.owner-1].allocation_protect;
        info.state=p.protection?FNV_VM_COMMIT:FNV_VM_RESERVE;info.protect=p.protection;info.type=FNV_VM_PRIVATE;}
    *out=info;r=result(sizeof(info),0,0);
done:unlock(v);return r;
}
struct fnv_vm_result fnv_vm_protect(struct fnv_guest_memory *v,uint32_t at,uint32_t bytes,uint32_t protection,uint32_t *old) {
    if(!v || !old)return result(0,INVALID,0);
    if(!protection_valid(protection))return result(0,NOT_SUPPORTED,0);
    lock(v);struct fnv_vm_result r=result(0,ADDRESS,0);uint32_t first,count;
    if(v->poisoned){r.error=FNV_VM_CORRUPT;goto done;}
    if(!span(v,at,bytes,4096,&first,&count) || !contained(v,first,count))goto done;
    for(uint32_t i=first;i<first+count;i++)if(!v->pages[i].protection)goto done;
    r=apply(v,FNV_VM_PROTECT_PAGES,first,count,protection);if(r.error)goto done;
    *old=v->pages[first].protection;
    for(uint32_t i=first;i<first+count;i++)v->pages[i].protection=protection;
    r=result(1,0,0);
done:unlock(v);return r;
}
int fnv_vm_access(struct fnv_guest_memory *v,uint32_t at,uint32_t bytes,int write_access) {
    if(!v)return 0;
    lock(v);uint32_t first,count;int ok=!v->poisoned && span(v,at,bytes,4096,&first,&count);
    if(ok)for(uint32_t i=first;i<first+count;i++) {
        uint32_t p=v->pages[i].protection;
        if(p!=4 && (write_access || p!=2)){ok=0;break;}
    }
    unlock(v);return ok;
}
void fnv_vm_get_counters(struct fnv_guest_memory *v,struct fnv_vm_counters *out) {
    if(v && out){lock(v);*out=(struct fnv_vm_counters){v->reserved*4096,v->committed*4096,v->config.commit_limit,v->poisoned};unlock(v);}
}
