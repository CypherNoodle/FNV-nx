#include <switch.h>
#include <stdatomic.h>
#include "fnv_sdk_runtime.h"
#include "engine_crash.h"

/* Verified against the installed libnx exception entry: stack is an array,
 * size is a u64 object and ignoredebug is a u32, not a stack pointer. */
_Alignas(16) unsigned char __nx_exception_stack[65536];
u64 __nx_exception_stack_size=sizeof(__nx_exception_stack);
u32 __nx_exception_ignoredebug=1;
extern void _start(void);
extern RECOMP_TLS uint32_t g_ebp;
static FsFile sink;
static int sink_ready;
static atomic_uint phase,handling;
static unsigned char record[16384];
static size_t used;
static const unsigned char *guest;
static uint32_t guest_base,guest_bytes;
static void text(const char *s){while(*s && used<sizeof(record))record[used++]=(unsigned char)*s++;}
static void hex(uint64_t value,unsigned digits){static const char chars[]="0123456789ABCDEF";for(unsigned i=digits;i;i--)if(used<sizeof(record))record[used++]=(unsigned char)chars[(value>>((i-1)*4))&15];}
static void field(const char *name,uint64_t value){text(name);text("=");hex(value,16);text("\n");}
static int readable(uintptr_t address,size_t bytes) {
    MemoryInfo info;u32 page;
    if(!bytes || address>UINTPTR_MAX-bytes || R_FAILED(svcQueryMemory(&info,&page,address)) || !(info.perm&1))return 0;
    return address>=info.addr && (uint64_t)address-info.addr<=info.size && bytes<=info.size-((uint64_t)address-info.addr);
}
static int guest_read(uint32_t address,void *out,size_t bytes) {
    if(!guest || address<guest_base || (uint64_t)address-guest_base+bytes>guest_bytes)return 0;
    const volatile unsigned char *p=guest+(address-guest_base);
    if(!readable((uintptr_t)p,bytes))return 0;
    for(size_t i=0;i<bytes;i++)((unsigned char *)out)[i]=p[i];
    return 1;
}
static void region(const char *name,uint32_t address) {
    text(name);text(" guest=");hex(address,8);
    if(guest && address>=guest_base && (uint64_t)address-guest_base<guest_bytes) {
        uintptr_t host=(uintptr_t)guest+(address-guest_base);MemoryInfo info;u32 page;
        text(" host=");hex(host,16);
        if(R_SUCCEEDED(svcQueryMemory(&info,&page,host))){text(" region=");hex(info.addr,16);text(" bytes=");hex(info.size,16);text(" type=");hex(info.type,8);text(" perm=");hex(info.perm,8);}
    }
    text("\n");
}
uint32_t fnv_engine_crash_prepare(const char *revision) {
    if(!revision || revision[0]!='h' || revision[1]!='w' || revision[2]<'0' || revision[2]>'9' || revision[3]<'0' || revision[3]>'9' || revision[4])return 87;
    FsFileSystem *sd=fsdevGetDeviceFileSystem("sdmc");if(!sd)return 2;
    char path[]="/switch/FNV-nx/fnv-pc-engine-hw00-crash.log";
    for(size_t i=0;path[i];i++)if(path[i]=='h' && path[i+1]=='w'){path[i+2]=revision[2];path[i+3]=revision[3];break;}
    /* Opening an existing revision log is deliberate: session header records
     * that no fault was caught yet, instead of leaving stale crash evidence. */
    Result result=fsFsCreateFile(sd,path,sizeof(record),0);
    (void)result;result=fsFsOpenFile(sd,path,FsOpenMode_Write,&sink);if(R_FAILED(result))return result;
    result=fsFileSetSize(&sink,sizeof(record));if(R_FAILED(result)){fsFileClose(&sink);return result;}
    used=0;text("Native crash sink initialized; no native fault captured in this session.\n");
    size_t header=used;while(used<sizeof(record))record[used++]=' ';
    result=fsFileWrite(&sink,0,record,sizeof(record),FsWriteOption_Flush);
    if(R_FAILED(result)){fsFileClose(&sink);return result;}
    used=header;sink_ready=1;return 0;
}
void fnv_engine_crash_guest(const unsigned char *memory,uint32_t base,uint32_t bytes){guest=memory;guest_base=base;guest_bytes=bytes;}
void fnv_engine_crash_phase(uint32_t value){atomic_store(&phase,value);}
void __libnx_exception_handler(ThreadExceptionDump *dump) {
    if(atomic_exchange(&handling,1))svcExitProcess();
    used=0;text("FNV native ARM64 exception; diagnostics only, execution cannot resume.\n");
    field("phase",atomic_load(&phase));uint64_t thread=0;(void)svcGetThreadId(&thread,CUR_THREAD_HANDLE);field("thread",thread);
    uintptr_t base=(uintptr_t)_start;field("image_base",base);
    field("error_desc",dump->error_desc);field("pc",dump->pc.x);field("pc_offset",dump->pc.x-base);
    field("lr",dump->lr.x);field("lr_offset",dump->lr.x-base);field("sp",dump->sp.x);field("fp",dump->fp.x);
    field("far",dump->far.x);field("esr",dump->esr);field("pstate",dump->pstate);
    for(unsigned i=0;i<29;i++){text("x");hex(i,2);text("=");hex(dump->cpu_gprs[i].x,16);text("\n");}
    field("guest_eax",g_eax);field("guest_ecx",g_ecx);field("guest_edx",g_edx);field("guest_ebx",g_ebx);
    field("guest_esp",g_esp);field("guest_ebp",g_ebp);field("guest_esi",g_esi);field("guest_edi",g_edi);field("guest_fs",g_fs_base);
    region("EDX",g_edx);region("ESI",g_esi);region("EDI",g_edi);region("ESP",g_esp);
    /* Persist the minimum record before probing stack contents. A secondary
     * fault must not discard the original PC and register evidence. */
    if(sink_ready)(void)fsFileWrite(&sink,0,record,used,FsWriteOption_Flush);
    text("guest_stack_u32 (address:value)\n");
    for(unsigned i=0;i<128;i++){uint64_t at=(uint64_t)g_esp+i*4;uint32_t value;
        if(at>UINT32_MAX || !guest_read((uint32_t)at,&value,4))break;
        hex(at,8);text(":");hex(value,8);text((i&3)==3?"\n":" ");}
    text("\nindirect_trace\n");field("trace_index",fnv_cpu_icall_trace_idx);field("trace_count",fnv_cpu_icall_count);
    for(unsigned i=0;i<16;i++){hex(fnv_cpu_icall_trace[i],8);text(i==15?"\n":" ");}
    text("native_frame_chain\n");uintptr_t fp=dump->fp.x;
    for(unsigned i=0;i<16;i++){if((fp&15) || !readable(fp,16))break;
        const volatile uint64_t *p=(const volatile uint64_t *)fp;uint64_t next=p[0],lr=p[1];
        field("return_offset",lr-base);if(next<=fp || next-fp>8u<<20)break;fp=next;}
    text("END native crash record\n");
    if(sink_ready){(void)fsFileWrite(&sink,0,record,used,FsWriteOption_Flush);(void)fsFileSetSize(&sink,used);(void)fsFileFlush(&sink);}
    svcExitProcess();
}
