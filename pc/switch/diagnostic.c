#include <switch.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_runtime.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "memory_horizon.h"

#define ROOT "sdmc:/switch/FNV-nx/"
#define ARENA_BYTES (132u*1024u*1024u)
static FILE *log_file;
static unsigned failures;
static void report(const char *fmt,...) {
    va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);
    if(log_file){va_start(args,fmt);vfprintf(log_file,fmt,args);va_end(args);fflush(log_file);}
    consoleUpdate(NULL);
}
static int check(int condition,const char *stage) {
    report("%s %s\n",condition?"PASS":"FAIL",stage);if(!condition)failures++;return condition;
}
static void put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void diagnostic(void *unused,const struct fnv_cpu_fault *f) {
    (void)unused;report("CPU fault kind=%u entry=%08lX address=%08lX %s\n",(unsigned)f->kind,
        (unsigned long)f->entry,(unsigned long)f->address,f->text);
}
static void bridge_probe(void) {
    uint32_t a=fnv_cpu_argument_u32(0),b=fnv_cpu_argument_u32(1);
    fnv_windows_set_last_error(0x12345678);
    fnv_cpu_return_u32(a^b,8);
}
static int identify(void) {
    FILE *file=fopen(ROOT "FalloutNV.exe","rb");
    if(!check(file!=NULL,"open FalloutNV.exe"))return 0;
    unsigned char *buffer=malloc(256*1024),digest[32];Sha256Context sha;
    if(!buffer){fclose(file);return check(0,"hash buffer allocation");}
    sha256ContextCreate(&sha);size_t n;uint64_t bytes=0;bool valid=true;
    while((n=fread(buffer,1,256*1024,file))!=0){
        bytes+=n;if(bytes>128u*1024*1024){valid=false;break;}
        sha256ContextUpdate(&sha,buffer,n);
    }
    if(ferror(file))valid=false;
    fclose(file);free(buffer);
    if(!check(valid,"bounded EXE read"))return 0;
    sha256ContextGetHash(&sha,digest);char hex[65];
    for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    report("EXE bytes=%llu SHA256=%s\n",(unsigned long long)bytes,hex);
    report("Identity: %s\n",!strcmp(hex,"3a87f92f011e5dc9179ddf733cf08be2b39ea6e5b7a8a9e3a9a72dafcc1b104d")?
        "examined Steam EXE (data inspection only)":"unprofiled EXE (data inspection only)");
    return 1;
}
static void run_probe(void) {
    failures=0;report("\nFNV PC diagnostic pc-diag01\n");
    report("Engine execution=disabled; D3D9 renderer=not connected\n");
    if(!identify())return;
    unsigned char *arena=memalign(4096,ARENA_BYTES);
    if(!check(arena!=NULL,"132 MiB guest backing"))return;
    struct fnv_native_image image;u64 start=armGetSystemTick();
    enum fnv_native_result loaded=fnv_native_load_image(ROOT "FalloutNV.exe",arena,128u*1024*1024,&image);
    report("PE load result=%u time_ms=%llu\n",(unsigned)loaded,(unsigned long long)armTicksToNs(armGetSystemTick()-start)/1000000);
    if(!check(loaded==FNV_NATIVE_OK,"map PE sections")){free(arena);return;}
    report("PE base=%08lX image=%lu entry=%08lX sections=%u TLS=%08lX delay=%08lX\n",
        (unsigned long)image.base,(unsigned long)image.size,(unsigned long)(image.base+image.entry_rva),image.section_count,
        (unsigned long)image.tls_rva,(unsigned long)image.delay_import_rva);
    struct fnv_native_import *imports=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*imports));size_t count=0;
    if(imports){enum fnv_native_result r=fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&count);
        report("Import inventory result=%u slots=%lu (IAT unchanged)\n",(unsigned)r,(unsigned long)count);
        check(r==FNV_NATIVE_OK,"collect normal imports");free(imports);}
    else check(0,"import inventory allocation");
    /* All diagnostic regions follow the image and are disjoint. No EXE entry
     * or captured game function is called, regardless of executable identity. */
    uint64_t extra=((uint64_t)image.size+65535)&~UINT64_C(65535);
    if(!check(extra+0x50000<=ARENA_BYTES && (uint64_t)image.base+ARENA_BYTES<=UINT32_MAX,"guest address budget")){free(arena);return;}
    memset(arena+(size_t)extra,0,0x50000);
    uint32_t tail=image.base+(uint32_t)extra,pool=tail+0x40000;
    struct fnv_vm_permissions permissions;fnv_vm_horizon_permissions(&permissions);
    struct fnv_guest_memory *vm=NULL;
    struct fnv_vm_config mc={arena+extra+0x40000,pool,0x10000,0x10000,&permissions,fnv_vm_eager_apply};
    struct fnv_vm_result mr=fnv_vm_create(&mc,&vm);
    report("VM initialize error=%lu native=%08lX\n",(unsigned long)mr.error,(unsigned long)mr.native_error);
    if(!check(!mr.error,"Horizon page permissions")){free(arena);return;}
    struct fnv_windows_config wc={0};wc.cpu.memory=arena;wc.cpu.guest_base=image.base;wc.cpu.memory_bytes=ARENA_BYTES;
    wc.cpu.code_low=image.base+image.entry_rva;wc.cpu.code_high=wc.cpu.code_low+1;wc.cpu.diagnostic=diagnostic;
    wc.image_bytes=image.size;wc.process_id=1;wc.peb_address=tail;wc.memory=vm;
    struct fnv_windows_process *process=NULL;uint32_t token=0;bool attached=false,registered=false;
    enum fnv_cpu_result cr=fnv_windows_create(&wc,&process);
    if(check(cr==FNV_CPU_OK,"Windows process + CPU runtime")) {
        struct fnv_cpu_runtime *cpu=fnv_windows_runtime(process);fnv_cpu_function probe=bridge_probe;
        cr=fnv_cpu_register_native(cpu,&probe,1,&token);registered=cr==FNV_CPU_OK;
        if(check(registered,"register diagnostic stdcall bridge")) {
            struct fnv_windows_thread_config tc={0};tc.thread_id=1;tc.teb_address=tail+0x1000;tc.tls_expansion_address=tail+0x2000;
            tc.stack_low=tail+0x10000;tc.stack_high=tail+0x20000;
            uint32_t esp=tc.stack_high-16;fnv_cpu_state_init(&tc.state,esp,tc.teb_address);tc.state.gpr[3]=0x76543210;
            unsigned char *stack=arena+(esp-image.base);put32(stack,0);put32(stack+4,0x1234);put32(stack+8,0xABCD);
            cr=fnv_windows_thread_attach(process,&tc);attached=cr==FNV_CPU_OK;
            if(check(attached,"attach TEB / libnx soft TLS")) {
                struct fnv_cpu_fault fault;cr=fnv_cpu_execute(token,&fault);struct fnv_cpu_state state;
                check(cr==FNV_CPU_OK && fnv_cpu_get_state(&state)==FNV_CPU_OK && state.gpr[0]==(0x1234^0xABCD) &&
                    state.gpr[4]==esp+12 && state.gpr[3]==0x76543210 && state.fs_base==tc.teb_address,"stdcall arguments, return and registers");
                check(get32(arena+(tc.teb_address-image.base)+0x34)==0x12345678,"TEB LastError write");
                mr=fnv_vm_allocate(vm,0,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);
                if(check(!mr.error,"VM reserve + commit")) {
                    unsigned char *p=arena+(mr.value-image.base);put32(p,0x11223344);uint32_t old=0;
                    struct fnv_vm_result pr=fnv_vm_protect(vm,mr.value,4096,FNV_VM_READONLY,&old);
                    check(!pr.error && old==FNV_VM_READWRITE && get32(p)==0x11223344,"VM readonly preserves bytes");
                    pr=fnv_vm_free(vm,mr.value,4096,FNV_VM_DECOMMIT);
                    if(check(!pr.error,"VM decommit")) {
                        pr=fnv_vm_allocate(vm,mr.value,4096,FNV_VM_COMMIT,FNV_VM_READWRITE);
                        check(!pr.error && get32(p)==0,"VM recommit zeroes memory");
                    }
                    check(!fnv_vm_free(vm,mr.value,0,FNV_VM_RELEASE).error,"VM release");
                }
            }
        }
        if(attached)check(fnv_windows_thread_detach()==FNV_CPU_OK,"thread detach");
        if(registered)check(fnv_cpu_unregister_native(cpu,token,1)==FNV_CPU_OK,"unregister bridge");
        check(fnv_windows_destroy(process)==FNV_CPU_OK,"process destroy");
    }
    mr=fnv_vm_destroy(vm);check(!mr.error,"restore backing permissions");
    if(!mr.error)free(arena); /* Keep backing owned if restoring permissions failed. */
    report("DIAGNOSTIC COMPLETE failures=%u engine_started=false\n",failures);
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;consoleInit(NULL);
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);PadState pad;padInitializeDefault(&pad);
    log_file=fopen(ROOT "fnv-pc-diag.log","a");
    report("\n--- pc-diag01 ---\nNRO path: " ROOT "fnv-nx.nro\n");
    if(!log_file)report("FAIL cannot open log in " ROOT "\n");
    report("Applet type=%u; use application mode\n",(unsigned)appletGetAppletType());
    if(appletGetAppletType()==AppletType_Application || appletGetAppletType()==AppletType_SystemApplication)run_probe();
    else report("Application mode required. Open hbmenu with R over a game.\n");
    report("A: repeat diagnostic   +: exit\n");
    while(appletMainLoop()) {
        padUpdate(&pad);u64 keys=padGetButtonsDown(&pad);
        if(keys&HidNpadButton_Plus)break;
        if((keys&HidNpadButton_A) && (appletGetAppletType()==AppletType_Application || appletGetAppletType()==AppletType_SystemApplication)){
            consoleClear();run_probe();report("A: repeat diagnostic   +: exit\n");
        }
        consoleUpdate(NULL);
    }
    report("Normal exit\n");if(log_file)fclose(log_file);consoleExit(NULL);return 0;
}
