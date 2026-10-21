#include <windows.h>
#include "fnv_sdk_runtime.h"
#include "windows_thread.h"
#include "windows_memory.h"
#include "windows_modules.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#undef NDEBUG
#include <assert.h>
#include "startup_profile.h"
#define BASE 0x400000u
static uint32_t arena_bytes;
#define ARENA arena_bytes
static unsigned char *arena;
uint32_t fnv_probe_site;
static void put32(uint32_t at,uint32_t value){assert(at>=BASE && (uint64_t)at+4<=BASE+ARENA);for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t permission(void *context,void *address,uint32_t bytes,uint32_t value){
    (void)context;DWORD old;return VirtualProtect(address,bytes,value==3?PAGE_READWRITE:value==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();
}
static int same(const struct fnv_cpu_export *e,const struct fnv_native_import *i){
    return !_stricmp(e->dll,i->dll) && e->by_ordinal==i->by_ordinal &&
        (i->by_ordinal?e->ordinal==i->ordinal:!strcmp(e->name,i->name));
}
static void string(const char *s){putchar('"');for(;*s;s++){unsigned c=(unsigned char)*s;if(c=='"' || c=='\\')putchar('\\');
    if(c>=32 && c<127)putchar((int)c);else printf("\\u%04x",c);}putchar('"');}
static uint32_t number(const char *text){
    assert(text && text[0]>='0' && text[0]<='9');errno=0;char *end;
    unsigned long long n=strtoull(text,&end,10);assert(!errno && !*end && n<=UINT32_MAX);return (uint32_t)n;
}
static int peek(uint32_t at,void *out,size_t bytes){
    SIZE_T got=0;
    return at>=BASE && (uint64_t)at+bytes<=(uint64_t)BASE+ARENA &&
        ReadProcessMemory(GetCurrentProcess(),arena+(at-BASE),out,bytes,&got) && got==bytes;
}
static void diagnostic_string(uint32_t at){
    char text[512];size_t n=0;const char *status="truncated";
    for(;n<sizeof(text)-1;n++){
        unsigned char c;
        if((uint64_t)at+n>UINT32_MAX || !peek(at+(uint32_t)n,&c,1)){status="unreadable";break;}
        if(!c){status="complete";break;}text[n]=(char)c;
    }
    text[n]=0;printf("{\"address\":%u,\"status\":",at);string(status);printf(",\"value\":");string(text);putchar('}');
}
static void message_box(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"USER32.dll!MessageBoxA"))return;
    uint32_t words[5];printf(",\"message_box\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"return_address\":%u,\"window\":%u,\"type\":%u,\"text\":",words[0],words[1],words[4]);
    diagnostic_string(words[2]);printf(",\"caption\":");diagnostic_string(words[3]);putchar('}');
}
int main(int argc,char **argv){
    assert(argc==6+FNV_STARTUP_CAPTURED_TEXT);
    arena_bytes=number(argv[argc-3]);uint32_t vm_bytes=number(argv[argc-2]),commit_limit=number(argv[argc-1]);
    assert(arena_bytes>0x2800000 && (uint64_t)BASE+arena_bytes<=UINT32_MAX &&
        vm_bytes==arena_bytes-0x2800000 && commit_limit && commit_limit<=vm_bytes &&
        !(arena_bytes&65535) && !(commit_limit&65535));
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
#if FNV_STARTUP_CAPTURED_TEXT
    assert(FNV_STARTUP_TEXT_RVA<image.size && FNV_STARTUP_TEXT_BYTES<=image.size-FNV_STARTUP_TEXT_RVA);
    unsigned char *text=fnv_native_guest_range(&image,image.base+FNV_STARTUP_TEXT_RVA,FNV_STARTUP_TEXT_BYTES);assert(text);
    FILE *captured=fopen(argv[3],"rb");assert(captured);
    assert(fread(text,1,FNV_STARTUP_TEXT_BYTES,captured)==FNV_STARTUP_TEXT_BYTES && fgetc(captured)==EOF && !ferror(captured));
    assert(fclose(captured)==0);
#endif
    struct fnv_vm_permissions permissions={NULL,permission};struct fnv_guest_memory *vm;
    struct fnv_vm_config v={arena+0x2800000,BASE+0x2800000,vm_bytes,commit_limit,&permissions,fnv_vm_eager_apply};
    assert(!fnv_vm_create(&v,&vm).error);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=0x1711;c.peb_address=BASE+0x2000000;
    c.memory=vm;c.heap_address=BASE+0x2400000;c.heap_bytes=0x400000;c.heap_capacity=65536;c.private_heap_reserve=0x1000000;c.private_heap_limit=8;
    c.parameters_address=BASE+0x2100000;c.parameters_bytes=0x10000;c.command_line="FalloutNV.exe";c.image_path="FalloutNV.exe";
    c.kernel_handle_capacity=1024;c.critical_section_capacity=256;c.file_root=argv[2];c.file_handle_capacity=128;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_cpu_export catalog[256],extras[FNV_NATIVE_MAX_IMPORTS];size_t known=0,extra_count=0,n;
    const struct fnv_cpu_export *e=fnv_windows_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;
    e=fnv_windows_modules_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_memory_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    struct fnv_windows_modules *modules;assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,known,&modules)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS],scratch[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&imported)==FNV_NATIVE_OK);
    assert(imported==sizeof(missing_imports)/sizeof(missing_imports[0]));unsigned supported=0;
    for(size_t i=0;i<imported;i++){
        int found=0;for(size_t j=0;j<known;j++)if(same(catalog+j,imports+i))found=1;
        if(found){supported++;continue;}
        for(size_t j=0;j<extra_count;j++)if(same(extras+j,imports+i))found=1;
        if(found)continue;
        extras[extra_count++]=(struct fnv_cpu_export){imports[i].dll,imports[i].by_ordinal?NULL:imports[i].name,imports[i].ordinal,imports[i].by_ordinal,missing_imports[i]};
    }
    size_t bound;assert(fnv_windows_bind_imports(process,&image,scratch,FNV_NATIVE_MAX_IMPORTS,extras,extra_count,&bound)==FNV_NATIVE_OK && bound==imported);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x2200000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0x24000;t.static_tls_vector=t.teb_address+8192;t.static_tls_vector_bytes=4096;
    t.static_tls_data=t.teb_address+0x30000;t.static_tls_data_bytes=0x10000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);put32(t.state.gpr[4],0);
    struct fnv_cpu_fault fault={0};enum fnv_cpu_result result=fnv_cpu_execute(FNV_STARTUP_ENTRY,&fault);
    assert(result==FNV_CPU_OK || result==FNV_CPU_GUEST_FAULT);
    struct fnv_windows_heap_stats heaps;assert(fnv_windows_heap_stats(process,&heaps)==FNV_CPU_OK);
    printf("{\"faulted\":%s,\"result\":%u,\"implemented_imports\":%u,\"trap_imports\":%u,\"fault_address\":%u,\"fault_kind\":%u,\"text\":",result==FNV_CPU_GUEST_FAULT?"true":"false",result,supported,(unsigned)imported-supported,fault.address,fault.kind);string(fault.text);
    printf(",\"last_indirect_site\":%u,\"trace_index\":%u,\"call_count\":%llu,\"registers\":[",fnv_probe_site,fault.trace_index,(unsigned long long)fault.call_count);
    for(unsigned i=0;i<8;i++)printf("%s%u",i?",":"",fault.state.gpr[i]);
    printf("],\"trace\":[");for(unsigned i=0;i<16;i++)printf("%s%u",i?",":"",fault.trace[i]);
    printf("],\"live_heaps\":%u,\"heap_blocks\":%u",heaps.live_heaps,heaps.live_blocks);
    struct fnv_vm_counters counters;fnv_vm_get_counters(vm,&counters);
    printf(",\"guest_memory\":{\"arena_bytes\":%u,\"vm_bytes\":%u,\"commit_limit\":%u,\"committed_bytes\":%u,\"reserved_bytes\":%u}",
        arena_bytes,vm_bytes,commit_limit,counters.committed_bytes,counters.reserved_bytes);
    message_box(&fault);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_modules_destroy(modules)==FNV_CPU_OK);
    enum fnv_cpu_result cleanup=fnv_windows_destroy(process);printf(",\"process_cleanup_result\":%u}\n",cleanup);
    /* A fault can leave live guest private heaps. In that case this isolated
     * host process owns the remaining arena until OS exit; never free it while
     * its WindowsProcess still references it. This is not normal engine exit. */
    if(cleanup==FNV_CPU_OK){assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));}
    return 0;
}
