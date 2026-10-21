/* Private captured-code CRT probe. No disk cold entry or engine frame. */
#include <switch.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fnv_sdk_runtime.h"
#include "windows_thread.h"
#include "windows_modules.h"
#include "windows_user32.h"
#include "windows_system.h"
#include "windows_profiles.h"
#include "windows_shell.h"
#include "windows_memory.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include "crt_profile.h"

#define ROOT "sdmc:/switch/FNV-nx/"
#define ARENA 0x4000000u
static FILE *log_file;
extern void fnv_crt_native_entry(void);
static void report(const char *fmt,...) {
    va_list a;va_start(a,fmt);vprintf(fmt,a);va_end(a);
    if(log_file){va_start(a,fmt);vfprintf(log_file,fmt,a);va_end(a);fflush(log_file);}
    consoleUpdate(NULL);
}
#define NEED(c,label) do { if(!(c)){report("FAIL %s\n",label);return;} } while(0)
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static int identify(const char *path,const char *expected) {
    FILE *f=fopen(path,"rb");if(!f){report("Missing: %s\n",path);return 0;}
    unsigned char *b=malloc(65536),digest[32];if(!b){fclose(f);return 0;}
    Sha256Context ctx;sha256ContextCreate(&ctx);size_t n;uint64_t total=0;int ok=1;
    while((n=fread(b,1,65536,f))){total+=n;if(total>128u*1024u*1024u){ok=0;break;}sha256ContextUpdate(&ctx,b,n);}
    if(ferror(f))ok=0;
    fclose(f);free(b);if(!ok)return 0;
    sha256ContextGetHash(&ctx,digest);char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",digest[i]);
    report("SHA256 %s %s (%llu bytes)\n",path,hex,(unsigned long long)total);return !strcmp(hex,expected);
}
static int same(const struct fnv_cpu_export *e,const struct fnv_native_import *i) {
    return !strcasecmp(e->dll,i->dll) && e->by_ordinal==i->by_ordinal &&
        (i->by_ordinal?e->ordinal==i->ordinal:!strcmp(e->name,i->name));
}
static void diagnostic(void *context,const struct fnv_cpu_fault *f) {
    (void)context;report("CPU diagnostic kind=%u address=%08X %s\n",(unsigned)f->kind,(unsigned)f->address,f->text);
}
static void run_probe(void) {
    report("FNV PC CRT AOT hw01: private capture, engine entry STOPPED\n");
    NEED(identify(ROOT "FalloutNV.exe",FNV_CRT_EXE_SHA256),"EXE identity (unsupported editions are not executed)");
    NEED(identify("romfs:/captured-text-data.bin",FNV_CRT_TEXT_SHA256),"private captured-text identity");
    unsigned char *arena=memalign(4096,ARENA);NEED(arena,"64 MiB aligned guest arena");memset(arena,0,ARENA);
    struct fnv_native_image image;
    NEED(fnv_native_load_image(ROOT "FalloutNV.exe",arena,ARENA,&image)==FNV_NATIVE_OK,"PE data load");
    NEED(image.base==FNV_CRT_BASE,"guest image base");
    unsigned char *text=fnv_native_guest_range(&image,image.base+FNV_CRT_TEXT_RVA,FNV_CRT_TEXT_BYTES);NEED(text,"text bounds");
    FILE *blob=fopen("romfs:/captured-text-data.bin","rb");NEED(blob,"private text blob");
    int copied=fread(text,1,FNV_CRT_TEXT_BYTES,blob)==FNV_CRT_TEXT_BYTES && fgetc(blob)==EOF && !ferror(blob);
    int closed=fclose(blob);NEED(copied && !closed,"capture-consistent text data");
    struct fnv_vm_permissions permissions;fnv_vm_horizon_permissions(&permissions);
    struct fnv_vm_config vm_config={arena+0x2800000,image.base+0x2800000,0x1800000,0x1000000,&permissions,fnv_vm_eager_apply};
    struct fnv_guest_memory *vm=NULL;struct fnv_vm_result vr=fnv_vm_create(&vm_config,&vm);
    report("VM error=%u native=%08X\n",(unsigned)vr.error,(unsigned)vr.native_error);NEED(!vr.error,"Horizon VM permissions");
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=image.base;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=image.base+0x1000;c.cpu.code_high=image.base+image.size;c.cpu.diagnostic=diagnostic;
    c.image_bytes=image.size;c.process_id=0x1711;c.peb_address=image.base+0x2000000;c.memory=vm;
    c.heap_address=image.base+0x2400000;c.heap_bytes=0x400000;c.heap_capacity=65536;c.private_heap_reserve=0x1000000;c.private_heap_limit=8;
    c.parameters_address=image.base+0x2100000;c.parameters_bytes=0x10000;c.command_line="FalloutNV.exe";c.image_path="FalloutNV.exe";
    c.kernel_handle_capacity=1024;c.critical_section_capacity=256;c.file_root="sdmc:/switch/FNV-nx";c.file_handle_capacity=128;
    struct fnv_windows_process *process=NULL;NEED(fnv_windows_create(&c,&process)==FNV_CPU_OK,"Windows process");
    fnv_cpu_function wrapper=fnv_crt_native_entry;uint32_t entry_token=0;
    NEED(fnv_cpu_register_native(fnv_windows_runtime(process),&wrapper,1,&entry_token)==FNV_CPU_OK,"private wrapper registration");
    struct fnv_windows_user32 *user32;NEED(fnv_windows_user32_create(process,&user32)==FNV_CPU_OK,"virtual desktop");
    struct fnv_windows_system *system;NEED(fnv_windows_system_create(process,3,&system)==FNV_CPU_OK,"guest CPU topology");
    struct fnv_cpu_export catalog[256],*extras=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*extras));size_t known=0,extra_count=0,n;
    NEED(extras,"native import scratch");
    const struct fnv_cpu_export *e=fnv_windows_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;
    e=fnv_windows_modules_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_memory_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_user32_exports(&n);NEED(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS,"USER32 catalog capacity");memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_shell_exports(&n);NEED(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS,"SHELL32 catalog capacity");memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    struct fnv_windows_modules *modules=NULL;
    NEED(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,known,&modules)==FNV_CPU_OK,"module catalog");
    struct fnv_native_import *imports=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*imports)),*scratch=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*scratch));size_t count;
    NEED(imports && scratch,"PE import scratch (off the main stack)");
    NEED(fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&count)==FNV_NATIVE_OK && count==FNV_CRT_IMPORT_COUNT,"import inventory");
    unsigned supported=0;
    for(size_t i=0;i<count;i++) {
        int found=0;for(size_t j=0;j<known;j++)if(same(catalog+j,imports+i))found=1;
        if(found){supported++;continue;}
        extras[extra_count++]=(struct fnv_cpu_export){imports[i].dll,imports[i].by_ordinal?NULL:imports[i].name,imports[i].ordinal,imports[i].by_ordinal,fnv_crt_missing_imports[i]};
    }
    size_t bound;NEED(fnv_windows_bind_imports(process,&image,scratch,FNV_NATIVE_MAX_IMPORTS,extras,extra_count,&bound)==FNV_NATIVE_OK && bound==count,"native IAT binding");
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=image.base+0x2200000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0x24000;t.static_tls_vector=t.teb_address+8192;t.static_tls_vector_bytes=4096;
    t.static_tls_data=t.teb_address+0x30000;t.static_tls_data_bytes=0x10000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    NEED(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK,"main guest thread");put32(arena+t.state.gpr[4]-image.base,0);
    report("Execute real captured CRT: imports=%u/%u, stop=%08X\n",supported,(unsigned)count,(unsigned)FNV_CRT_STOP);
    struct fnv_cpu_fault fault={0};u64 begin=armGetSystemTick();enum fnv_cpu_result result=fnv_cpu_execute(entry_token,&fault);
    report("result=%u kind=%u address=%08X calls=%llu time_ms=%llu %s\n",(unsigned)result,(unsigned)fault.kind,(unsigned)fault.address,
           (unsigned long long)fault.call_count,(unsigned long long)(armTicksToNs(armGetSystemTick()-begin)/1000000),fault.text);
    for(unsigned i=0;i<8;i++)report("gpr[%u]=%08X\n",i,(unsigned)fault.state.gpr[i]);
    for(unsigned i=0;i<16;i++)report("trace[%u]=%08X\n",i,(unsigned)fault.trace[i]);
    report("%s CRT reached deliberate engine boundary; engine frame=no\n",
           result==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED && fault.address==FNV_CRT_STOP &&
           !strcmp(fault.text,"Reached engine entry (diagnostic stop)")?"PASS":"FAIL");
    struct fnv_windows_heap_stats h;if(fnv_windows_heap_stats(process,&h)==FNV_CPU_OK)report("heaps=%u blocks=%u\n",(unsigned)h.live_heaps,(unsigned)h.live_blocks);
    NEED(fnv_windows_thread_detach()==FNV_CPU_OK,"thread detach");NEED(fnv_windows_modules_destroy(modules)==FNV_CPU_OK,"module detach");
    NEED(fnv_windows_user32_destroy(user32)==FNV_CPU_OK,"virtual desktop detach");
    NEED(fnv_windows_system_destroy(system)==FNV_CPU_OK,"system detach");
    enum fnv_cpu_result cleanup=fnv_windows_destroy(process);report("cleanup=%u (live guest heaps may remain until process exit)\n",(unsigned)cleanup);
    if(cleanup==FNV_CPU_OK){NEED(!fnv_vm_destroy(vm).error,"VM release");free(extras);free(imports);free(scratch);free(arena);}
}
int main(void) {
    consoleInit(NULL);log_file=fopen(ROOT "fnv-pc-crt-hw01.log","w");
    report("FNV PC CRT AOT hw01\nA: run once   +: exit\nNo game graphics/audio in this diagnostic.\n");
    Result romfs=romfsInit();report("romfs=%08X\n",(unsigned)romfs);PadState pad;padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&pad);int ran=0;
    while(appletMainLoop()) {padUpdate(&pad);u64 down=padGetButtonsDown(&pad);if(down&HidNpadButton_Plus)break;
        if(!ran && (down&HidNpadButton_A)){ran=1;if(R_SUCCEEDED(romfs))run_probe();else report("FAIL romfs unavailable\n");report("Finished. +: exit\n");}
        consoleUpdate(NULL);
    }
    if(R_SUCCEEDED(romfs))romfsExit();
    if(log_file)fclose(log_file);
    consoleExit(NULL);return 0;
}
