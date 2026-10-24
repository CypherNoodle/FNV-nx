#include <windows.h>
#include "windows_display.h"
#include "windows_display_win32.h"
#include "windows_display_resolution.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define RECORD (BASE+0x80000)
#define PARENT (BASE+0x81000)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT&&fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    assert(r==FNV_CPU_OK&&fnv_cpu_get_state(&a)==FNV_CPU_OK&&a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
static unsigned checks,adapters,monitors;
static int compare(const char *parent,uint32_t index,uint32_t flags,uint32_t cb){
    unsigned char native[440];memset(native,0xa5,sizeof(native));memset(arena+RECORD-BASE,0xa5,sizeof(native));memcpy(native,&cb,4);put(RECORD,cb);
    if(parent)strcpy((char *)arena+PARENT-BASE,parent);
    uint32_t args[]={parent?PARENT:0,index,RECORD,flags},error[]={0x12345678};call(2,error,1,0);
    SetLastError(error[0]);BOOL expected=EnumDisplayDevicesA(parent,index,(DISPLAY_DEVICEA *)native,flags);DWORD last=GetLastError();
    uint32_t actual=call(0,args,4,0);assert(actual==(uint32_t)!!expected && last==error[0] && call(1,NULL,0,0)==last);
    if(memcmp(native,arena+RECORD-BASE,sizeof(native))){fprintf(stderr,"record mismatch cb%u index%u flags%u\n",cb,index,flags);assert(0);}
    checks++;return expected!=0;
}
static int bad_backend(void *context,const char *parent,uint32_t index,uint32_t flags,struct fnv_display_device *out){
    (void)parent;(void)index;(void)flags;
    if(context)return -1;
    memset(out,'X',sizeof(*out));return 1;
}
int main(int argc,char **argv){
    assert(argc==2||argc==3);_Static_assert(sizeof(DISPLAY_DEVICEA)==424,"Windows ANSI display ABI");
    arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_display_backend backend={NULL,fnv_display_enumerate_win32};
    if(argc==3&&!strcmp(argv[2],"--strict-no-backend"))backend.enumerate=NULL;
    if(argc==3&&(!strcmp(argv[2],"--strict-backend-error")||!strcmp(argv[2],"--strict-record"))){backend.enumerate=bad_backend;backend.context=!strcmp(argv[2],"--strict-backend-error")?(void *)1:NULL;}
    struct fnv_windows_display *display;assert(fnv_windows_display_create(process,&backend,&display)==FNV_CPU_OK);
    struct fnv_user32_metrics metrics={NULL,fnv_display_metrics_win32};struct fnv_windows_user32 *user32;
    assert(fnv_windows_user32_create_with_metrics(process,&metrics,&user32)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_display_exports(&n);assert(n==1);struct fnv_cpu_export catalog[4];catalog[0]=e[0];
    e=fnv_windows_user32_exports(&n);assert(n==3);memcpy(catalog+1,e,n*sizeof(*e));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,catalog,4,&bound)==FNV_NATIVE_OK&&bound==4);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && fnv_windows_display_destroy(display)==FNV_CPU_BUSY);
    if(argc==3){
        uint32_t args[]={0,0,RECORD,0};put(RECORD,424);
        if(!strcmp(argv[2],"--strict-output"))args[2]=BASE+BYTES-2;
        if(!strcmp(argv[2],"--strict-parent"))args[0]=BASE+BYTES;
        if(!strcmp(argv[2],"--strict-parent-long")){memset(arena+PARENT-BASE,'X',32);args[0]=PARENT;}
        if(!strcmp(argv[2],"--strict-flags"))args[3]=2;
        if(!strcmp(argv[2],"--strict-size"))put(RECORD,UINT32_MAX);
        call(0,args,4,1);
    }else {
        for(unsigned flag=0;flag<2;flag++)for(unsigned i=0;i<64;i++){
            DISPLAY_DEVICEA d={0};d.cb=sizeof(d);BOOL found=EnumDisplayDevicesA(NULL,i,&d,flag);
            assert(compare(NULL,i,flag,424)==!!found);if(!found)break;adapters++;
            for(unsigned j=0;j<64;j++){int got=compare(d.DeviceName,j,flag,424);if(!got)break;monitors++;}
        }
        const uint32_t sizes[]={0,1,3,4,8,36,164,168,296,423,424,428};
        for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){compare(NULL,0,0,sizes[i]);compare(NULL,UINT32_MAX,0,sizes[i]);}
        compare("FNV_missing_display",0,0,424);compare("",0,0,424);assert(adapters>0);
        const uint32_t indices[]={0,1,76,77,78,79,80};
        for(unsigned i=0;i<7;i++){uint32_t a[]={indices[i]};assert(call(3,a,1,0)==(uint32_t)GetSystemMetrics((int)indices[i]));}
        assert(call(1,NULL,0,0)==0x12345678);
        struct fnv_display_resolution handheld={0,1280,720,1280,720},dock={1,1920,1080,1280,720};int32_t value=-1;
        assert(fnv_display_resolution_changed(&handheld,&dock)==1 && fnv_display_resolution_metrics(&dock,0,&value)==1 && value==1280);
        dock.surface_width=1920;dock.surface_height=1080;
        assert(fnv_display_resolution_metrics(&dock,0,&value)==1 && value==1920 && fnv_display_resolution_metrics(&dock,1,&value)==1 && value==1080);
        assert(fnv_display_resolution_metrics(&handheld,0,&value)==1 && value==1280);
        assert(fnv_display_resolution_changed(&handheld,&handheld)==0);
        dock.output_width=0;assert(fnv_display_resolution_metrics(&dock,0,&value)==-1);


    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_windows_display_destroy(display)==FNV_CPU_OK&&fnv_windows_user32_destroy(user32)==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK&&VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    printf("{\"status\":\"PASS\",\"native_display_records_compared\":%u,\"adapter_queries_found\":%u,\"monitor_queries_found\":%u,\"display_metric_checks\":7,\"handheld_dock_policy_checked\":true,\"last_error_preserved\":true,\"engine_started\":false}\n",checks,adapters,monitors);return 0;
}
