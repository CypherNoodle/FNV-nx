#include "fnv_sdk_runtime.h"
#include "cpu_runtime.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(ptrdiff_t)>=8, "64-bit host required");
_Static_assert(ICALL_TRACE_SIZE==16, "Review SDK trace layout");
_Static_assert(sizeof(RecompXmm)==16 && sizeof(RecompMmx)==8, "Review SDK vector layout");
ptrdiff_t g_xbox_mem_offset;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi;
RECOMP_TLS double g_fp_stack[8];
RECOMP_TLS int g_fp_top,g_df,g_fp_cmp;
RECOMP_TLS uint16_t g_fp_control_word,g_fp_cc;
RECOMP_TLS uint32_t g_fs_base,g_seh_ebp;
RECOMP_TLS RecompXmm g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7;
RECOMP_TLS RecompMmx g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7;
RECOMP_TLS volatile uint32_t fnv_cpu_icall_trace[16],fnv_cpu_icall_trace_idx;
RECOMP_TLS volatile uint64_t fnv_cpu_icall_count;

struct fnv_cpu_runtime {
    struct fnv_cpu_config config;
    fnv_cpu_function imports[FNV_NATIVE_MAX_IMPORTS], staging[FNV_NATIVE_MAX_IMPORTS];
    size_t imports_count, users;
    int bound;
    atomic_int faulted;
};
static struct fnv_cpu_runtime *active;
static atomic_flag lifecycle=ATOMIC_FLAG_INIT;
static RECOMP_TLS struct fnv_cpu_runtime *thread_runtime;
static RECOMP_TLS jmp_buf *fault_boundary;
static RECOMP_TLS struct fnv_cpu_fault thread_fault;
static RECOMP_TLS uint32_t thread_entry;
static RECOMP_TLS uint64_t last_timestamp;
static RECOMP_TLS int has_timestamp;
static void lock(void) { while(atomic_flag_test_and_set_explicit(&lifecycle,memory_order_acquire)) {} }
static void unlock(void) { atomic_flag_clear_explicit(&lifecycle,memory_order_release); }

static void save_state(struct fnv_cpu_state *s) {
    RecompXmm *x[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    RecompMmx *m[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7};
    memset(s,0,sizeof(*s));
    s->gpr[0]=g_eax;s->gpr[1]=g_ecx;s->gpr[2]=g_edx;s->gpr[3]=g_ebx;
    s->gpr[4]=g_esp;s->gpr[5]=g_ebp;s->gpr[6]=g_esi;s->gpr[7]=g_edi;
    s->fs_base=g_fs_base;s->seh_ebp=g_seh_ebp;memcpy(s->x87,g_fp_stack,sizeof(s->x87));
    s->x87_top=g_fp_top;s->direction=g_df;s->fp_compare=g_fp_cmp;
    s->fp_control=g_fp_control_word;s->fp_status=g_fp_cc;
    for(unsigned i=0;i<8;i++){memcpy(s->xmm[i],x[i],16);s->mmx[i]=m[i]->q;}
}
static void restore_state(const struct fnv_cpu_state *s) {
    RecompXmm *x[]={&g_xmm0,&g_xmm1,&g_xmm2,&g_xmm3,&g_xmm4,&g_xmm5,&g_xmm6,&g_xmm7};
    RecompMmx *m[]={&g_mm0,&g_mm1,&g_mm2,&g_mm3,&g_mm4,&g_mm5,&g_mm6,&g_mm7};
    g_eax=s->gpr[0];g_ecx=s->gpr[1];g_edx=s->gpr[2];g_ebx=s->gpr[3];
    g_esp=s->gpr[4];g_ebp=s->gpr[5];g_esi=s->gpr[6];g_edi=s->gpr[7];
    g_fs_base=s->fs_base;g_seh_ebp=s->seh_ebp;memcpy(g_fp_stack,s->x87,sizeof(s->x87));
    g_fp_top=s->x87_top;g_df=s->direction;g_fp_cmp=s->fp_compare;
    g_fp_control_word=s->fp_control;g_fp_cc=s->fp_status;
    for(unsigned i=0;i<8;i++){memcpy(x[i],s->xmm[i],16);m[i]->q=s->mmx[i];}
}
static int within(const struct fnv_cpu_runtime *r,uint32_t at,size_t bytes) {
    uint64_t end=(uint64_t)r->config.guest_base+r->config.memory_bytes;
    return at>=r->config.guest_base && at<=end && bytes<=end-at;
}
static int valid_state(const struct fnv_cpu_runtime *r,const struct fnv_cpu_state *s) {
    return s && s->x87_top>=0 && s->x87_top<8 && (s->direction==0 || s->direction==1) &&
        s->fp_compare>=-1 && s->fp_compare<=2 && within(r,s->gpr[4],4) &&
        (!s->fs_base || within(r,s->fs_base,4));
}
static _Noreturn void fail(enum fnv_cpu_fault_kind kind,uint32_t address_value,uint32_t detail,const char *text) {
    if(thread_runtime && fault_boundary) {
        memset(&thread_fault,0,sizeof(thread_fault));thread_fault.kind=kind;
        thread_fault.entry=thread_entry;thread_fault.address=address_value;thread_fault.detail=detail;
        thread_fault.trace_index=fnv_cpu_icall_trace_idx;thread_fault.call_count=fnv_cpu_icall_count;
        for(unsigned i=0;i<16;i++)thread_fault.trace[i]=fnv_cpu_icall_trace[i];
        save_state(&thread_fault.state);snprintf(thread_fault.text,sizeof(thread_fault.text),"%s",text);
        atomic_store_explicit(&thread_runtime->faulted,1,memory_order_release);
        if(thread_runtime->config.diagnostic)thread_runtime->config.diagnostic(thread_runtime->config.context,&thread_fault);
        longjmp(*fault_boundary,1);
    }
    fprintf(stderr,"FNV CPU fault outside execution boundary: %s (%08x)\n",text,(unsigned)address_value);
    abort();
}

enum fnv_cpu_result fnv_cpu_create(const struct fnv_cpu_config *c,struct fnv_cpu_runtime **out) {
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;
    if(!c || !c->memory || !c->memory_bytes || (uint64_t)c->guest_base+c->memory_bytes>UINT64_C(0x100000000) ||
        c->code_low<c->guest_base || c->code_low>=c->code_high || c->code_high>=0xFE000000u ||
        c->code_high>(uint64_t)c->guest_base+c->memory_bytes || (c->timestamp && !c->timestamp_hz))return FNV_CPU_INVALID;
    struct fnv_cpu_runtime *r=calloc(1,sizeof(*r));if(!r)return FNV_CPU_NO_MEMORY;
    r->config=*c;atomic_init(&r->faulted,0);
    lock();if(active){unlock();free(r);return FNV_CPU_BUSY;}
    active=r;g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)c->memory-(ptrdiff_t)c->guest_base;
    g_xbox_code_lo=c->code_low;g_xbox_code_hi=c->code_high;unlock();*out=r;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_cpu_destroy(struct fnv_cpu_runtime *r) {
    lock();if(!r || active!=r){unlock();return FNV_CPU_INVALID;}
    if(r->users){unlock();return FNV_CPU_BUSY;}
    active=NULL;g_xbox_mem_offset=0;g_xbox_code_lo=g_xbox_code_hi=0;unlock();free(r);return FNV_CPU_OK;
}
static int dll_equal(const char *a,const char *b) {
    while(*a && *b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z')x+='a'-'A';
        if(y>='A' && y<='Z')y+='a'-'A';
        if(x!=y)return 0;}
    return *a==*b;
}
static int bounded_string(const char *s,size_t max,int module) {
    if(!s || !*s)return 0;
    for(size_t i=0;i<max;i++){unsigned c=(unsigned char)s[i];if(!c)return 1;
        if(c>127 || (module && !((c>='a'&&c<='z') || (c>='A'&&c<='Z') ||
            (c>='0'&&c<='9') || c=='.' || c=='_' || c=='-')))return 0;}
    return 0;
}
struct binding { struct fnv_cpu_runtime *runtime;const struct fnv_cpu_export *exports;size_t count,index; };
static int resolve(void *opaque,const struct fnv_native_import *item,uint32_t *token) {
    struct binding *b=opaque;
    for(size_t i=0;i<b->count;i++){const struct fnv_cpu_export *e=b->exports+i;
        if(dll_equal(item->dll,e->dll) && item->by_ordinal==e->by_ordinal &&
            (e->by_ordinal?item->ordinal==e->ordinal:!strcmp(item->name,e->name))){
            if(b->index>=FNV_NATIVE_MAX_IMPORTS)return 0;
            b->runtime->staging[b->index]=e->function;*token=0xFE000000u+(uint32_t)b->index*4u;b->index++;return 1;}}
    return 0;
}
enum fnv_native_result fnv_cpu_bind_imports(struct fnv_cpu_runtime *r,struct fnv_native_image *image,
        struct fnv_native_import *scratch,size_t capacity,const struct fnv_cpu_export *exports,size_t count,size_t *out_count) {
    if(out_count)*out_count=0;
    if(!r || !image || !out_count || count>FNV_NATIVE_MAX_IMPORTS || (count && !exports))return FNV_NATIVE_FORMAT;
    for(size_t i=0;i<count;i++){const struct fnv_cpu_export *e=exports+i;
        if(!e->function || !bounded_string(e->dll,128,1) || (e->by_ordinal!=0 && e->by_ordinal!=1) ||
            (!e->by_ordinal && !bounded_string(e->name,256,0)))return FNV_NATIVE_FORMAT;
        for(size_t j=0;j<i;j++)if(dll_equal(e->dll,exports[j].dll) && e->by_ordinal==exports[j].by_ordinal &&
            (e->by_ordinal?e->ordinal==exports[j].ordinal:!strcmp(e->name,exports[j].name)))return FNV_NATIVE_FORMAT;
    }
    lock();if(active!=r || r->users || r->bound || image->data!=r->config.memory || image->base!=r->config.guest_base ||
        image->size>r->config.memory_bytes){unlock();return FNV_NATIVE_FORMAT;}
    struct binding b={r,exports,count,0};
    enum fnv_native_result status=fnv_native_bind_imports(image,scratch,capacity,resolve,&b,out_count);
    if(status==FNV_NATIVE_OK){memcpy(r->imports,r->staging,b.index*sizeof(r->imports[0]));r->imports_count=b.index;r->bound=1;}
    memset(r->staging,0,sizeof(r->staging));unlock();return status;
}
void fnv_cpu_state_init(struct fnv_cpu_state *s,uint32_t stack,uint32_t fs) {
    if(s){memset(s,0,sizeof(*s));s->gpr[4]=stack;s->fs_base=fs;s->fp_control=0x037F;}
}
enum fnv_cpu_result fnv_cpu_attach(struct fnv_cpu_runtime *r,const struct fnv_cpu_state *s) {
    if(thread_runtime)return FNV_CPU_BUSY;
    lock();if(!r || active!=r || !valid_state(r,s)){unlock();return FNV_CPU_INVALID;}
    if(atomic_load_explicit(&r->faulted,memory_order_acquire)){unlock();return FNV_CPU_FAULTED;}
    r->users++;thread_runtime=r;unlock();restore_state(s);has_timestamp=0;last_timestamp=0;
    memset(&thread_fault,0,sizeof(thread_fault));fnv_cpu_icall_trace_idx=0;fnv_cpu_icall_count=0;
    for(unsigned i=0;i<16;i++)fnv_cpu_icall_trace[i]=0;
    return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_cpu_detach(void) {
    if(!thread_runtime)return FNV_CPU_NOT_ATTACHED;
    if(fault_boundary)return FNV_CPU_BUSY;
    lock();thread_runtime->users--;thread_runtime=NULL;unlock();return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_cpu_get_state(struct fnv_cpu_state *s) {
    if(!thread_runtime)return FNV_CPU_NOT_ATTACHED;
    if(!s)return FNV_CPU_INVALID;
    save_state(s);return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_cpu_set_state(const struct fnv_cpu_state *s) {
    if(!thread_runtime)return FNV_CPU_NOT_ATTACHED;
    if(fault_boundary)return FNV_CPU_BUSY;
    if(atomic_load_explicit(&thread_runtime->faulted,memory_order_acquire))return FNV_CPU_FAULTED;
    if(!valid_state(thread_runtime,s))return FNV_CPU_INVALID;
    restore_state(s);return FNV_CPU_OK;
}
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va;return NULL; /* No overrides are configured. */ }
recomp_func_t recomp_lookup_kernel(uint32_t va) {
    if(!thread_runtime || va<0xFE000000u || ((va-0xFE000000u)&3u))return NULL;
    uint32_t index=(va-0xFE000000u)/4u;
    return index<thread_runtime->imports_count?thread_runtime->imports[index]:NULL;
}
void recomp_unimpl(const char *text,uint32_t va) { fail(FNV_CPU_UNIMPLEMENTED,va,0,text?text:"unimplemented instruction"); }
void recomp_icall_fail_log(uint32_t va) { fail(FNV_CPU_UNKNOWN_TARGET,va,0,"unknown indirect target"); }
void recomp_icall_not_code_log(uint32_t va) { fail(FNV_CPU_NOT_CODE,va,0,"indirect target is not code"); }
void recomp_cpuid(void) {
    uint32_t out[4]={0};uint32_t leaf=g_eax,subleaf=g_ecx;
    if(!thread_runtime || !thread_runtime->config.cpuid || !thread_runtime->config.cpuid(thread_runtime->config.context,leaf,subleaf,out))
        fail(FNV_CPU_CPUID_UNAVAILABLE,0,leaf,"CPUID provider unavailable");
    g_eax=out[0];g_ebx=out[1];g_ecx=out[2];g_edx=out[3];
}
uint64_t xbox_ReadTimeStampCounter(void) {
    uint64_t ticks=0;
    if(!thread_runtime || !thread_runtime->config.timestamp || !thread_runtime->config.timestamp(thread_runtime->config.context,&ticks))
        fail(FNV_CPU_CLOCK_UNAVAILABLE,0,0,"guest timestamp provider unavailable");
    if(has_timestamp && ticks<last_timestamp)fail(FNV_CPU_CLOCK_BACKWARDS,0,0,"guest timestamp moved backwards");
    last_timestamp=ticks;has_timestamp=1;return ticks;
}
enum fnv_cpu_result fnv_cpu_execute(uint32_t entry,struct fnv_cpu_fault *out) {
    if(out)memset(out,0,sizeof(*out));
    if(!thread_runtime)return FNV_CPU_NOT_ATTACHED;
    if(fault_boundary)return FNV_CPU_BUSY;
    if(atomic_load_explicit(&thread_runtime->faulted,memory_order_acquire))return FNV_CPU_FAULTED;
    jmp_buf boundary;thread_entry=entry;fault_boundary=&boundary;
    if(setjmp(boundary)==0){
        if(!RECOMP_ICALL_IS_CODE(entry))recomp_icall_not_code_log(entry);
        fnv_cpu_icall_trace[fnv_cpu_icall_trace_idx++&15]=entry;fnv_cpu_icall_count++;
        recomp_func_t fn=recomp_lookup_manual(entry);if(!fn)fn=recomp_lookup(entry);if(!fn)fn=recomp_lookup_kernel(entry);
        if(!fn)recomp_icall_fail_log(entry);
        fn();fault_boundary=NULL;
        if(atomic_load_explicit(&thread_runtime->faulted,memory_order_acquire)){
            memset(&thread_fault,0,sizeof(thread_fault));thread_fault.kind=FNV_CPU_PEER_FAULT;
            thread_fault.entry=entry;save_state(&thread_fault.state);if(out)*out=thread_fault;return FNV_CPU_GUEST_FAULT;}
        return FNV_CPU_OK;
    }
    fault_boundary=NULL;if(out)*out=thread_fault;return FNV_CPU_GUEST_FAULT;
}
void *fnv_cpu_memory(uint32_t address_value,size_t bytes) {
    if(!thread_runtime || !within(thread_runtime,address_value,bytes))fail(FNV_CPU_BAD_MEMORY,address_value,0,"guest argument outside flat arena");
    return thread_runtime->config.memory+(address_value-thread_runtime->config.guest_base);
}
uint32_t fnv_cpu_argument_u32(unsigned index) {
    uint64_t at=(uint64_t)g_esp+4u+(uint64_t)index*4u;
    if(at>UINT32_MAX)fail(FNV_CPU_BAD_MEMORY,g_esp,index,"guest argument address wraps");
    const unsigned char *p=fnv_cpu_memory((uint32_t)at,4);
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
void fnv_cpu_return_u32(uint32_t value,uint32_t bytes) {
    uint64_t next=(uint64_t)g_esp+4u+bytes;
    if((bytes&3u) || next>UINT32_MAX || !thread_runtime || !within(thread_runtime,g_esp,(size_t)bytes+4u))
        fail(FNV_CPU_BAD_MEMORY,g_esp,bytes,"invalid guest return frame");
    g_eax=value;g_esp=(uint32_t)next;
}
