#include <windows.h>
#include "windows_modules.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define NAME_TEXT (BASE+0x9000)
#define PROC_TEXT (BASE+0x9200)
enum {LOAD_A,LOAD_W,GET_A,GET_W,GET_PROC,FREE,GET_ERROR,SET_ERROR,GET_ERROR_ALIAS};
static const unsigned arguments[]={1,1,1,1,2,1,0,1,0};
static unsigned char *arena;static struct fnv_windows_process *process;
static uint32_t permissions(void *context,void *at,uint32_t bytes,uint32_t protection){
    (void)context;DWORD old;return VirtualProtect(at,bytes,protection==3?PAGE_READWRITE:protection==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();
}
static struct fnv_vm_permissions vm_backend={NULL,permissions};
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static struct fnv_windows_thread_config config(unsigned index){
    struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x10000;
    t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static enum fnv_cpu_result execute(uint32_t entry,const uint32_t *args,unsigned count,uint32_t *value,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(stack,0x401500);
    for(unsigned i=0;i<count;++i)put32(stack+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(entry,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+count*4);*value=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b){uint32_t args[]={a,b},v=0;struct fnv_cpu_fault f;
    enum fnv_cpu_result r=execute(0x401500+api*32,args,arguments[api],&v,&f);
    if(r!=FNV_CPU_OK)fprintf(stderr,"module API %u: %s\n",api,f.text);assert(r==FNV_CPU_OK);return v;}
static uint32_t indirect(uint32_t token){uint32_t v;struct fnv_cpu_fault f;assert(execute(0x401800,&token,1,&v,&f)==FNV_CPU_OK);return v;}
static uint32_t text(uint32_t at,const char *s,int wide){
    size_t n=strlen(s);for(size_t i=0;i<=n;++i){arena[at-BASE+i*(wide?2:1)]=(unsigned char)s[i];if(wide)arena[at-BASE+i*2+1]=0;}return at;
}
static void native_value(void){fnv_cpu_return_u32(0x1234abcd,0);}
static DWORD WINAPI worker(void *context){unsigned index=(unsigned)(uintptr_t)context;struct fnv_windows_thread_config t=config(index);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    for(unsigned i=0;i<500;++i){uint32_t module=call(LOAD_A,NAME_TEXT,0);assert(module);
        uint32_t token=call(GET_PROC,module,PROC_TEXT);assert(token && indirect(token)==0x1234abcd);assert(call(FREE,module,0));}
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_guest_memory *vm=NULL;
    if(argc==3 && !strcmp(argv[2],"--strict-vm")){
        struct fnv_vm_config v={0};v.backing=arena+0xe0000;v.base=BASE+0xe0000;v.bytes=65536;v.commit_limit=65536;
        v.context=&vm_backend;v.apply=fnv_vm_eager_apply;
        assert(!fnv_vm_create(&v,&vm).error);
        assert(fnv_vm_allocate(vm,v.base,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_NOACCESS).value==v.base);
    }
    struct fnv_windows_config c={0};c.memory=vm;c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t builtins,apis;const struct fnv_cpu_export *win=fnv_windows_exports(&builtins),*module_apis=fnv_windows_modules_exports(&apis);
    assert(builtins==FNV_WINDOWS_THREAD_EXPORTS && apis==6);struct fnv_cpu_export catalog[128],extra[9];memcpy(catalog,win,builtins*sizeof(*win));
    memcpy(extra,module_apis,apis*sizeof(*module_apis));
    char dll[]="Fixture.DLL",name[]="Value";
    extra[6]=(struct fnv_cpu_export){dll,name,0,0,native_value};extra[7]=(struct fnv_cpu_export){dll,NULL,7,1,native_value};
    extra[8]=(struct fnv_cpu_export){"other.dll","Value",0,0,native_value};
    memcpy(catalog+builtins,extra,sizeof(extra));size_t total=builtins+9;
    struct fnv_windows_modules *m=NULL;
    if(argc==3 && !strcmp(argv[2],"--strict-capacity")){
        fnv_cpu_function functions[FNV_CPU_MAX_NATIVE_METHODS];uint32_t tokens[FNV_CPU_MAX_NATIVE_METHODS];
        for(unsigned i=0;i<FNV_CPU_MAX_NATIVE_METHODS;++i)functions[i]=native_value;
        assert(fnv_cpu_register_native(fnv_windows_runtime(process),functions,FNV_CPU_MAX_NATIVE_METHODS,tokens)==FNV_CPU_OK);
        assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,total,&m)==FNV_CPU_NO_MEMORY && !m);
        assert(fnv_cpu_unregister_native(fnv_windows_runtime(process),tokens[0],FNV_CPU_MAX_NATIVE_METHODS)==FNV_CPU_OK);
        assert(fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    int no_provider=argc==3 && !strcmp(argv[2],"--strict-provider");
    if(!no_provider){
        struct fnv_cpu_export invalid[2]={extra[6],extra[6]};
        assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",invalid,2,&m)==FNV_CPU_INVALID && !m);
        assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,total,&m)==FNV_CPU_OK);
        struct fnv_windows_modules *second=NULL;assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,total,&second)==FNV_CPU_BUSY && !second);
        memset(dll,'x',sizeof(dll)-1);memset(name,'y',sizeof(name)-1); /* Owned namespace snapshot. */
        extra[6].dll="fixture.dll";extra[6].name="Value";extra[7].dll="fixture.dll";
    }
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,extra,9,&count)==FNV_NATIVE_OK && count==9);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={BASE+ARENA-1,0},v=0;struct fnv_cpu_fault fault;unsigned api=LOAD_A;
        if(!strcmp(argv[2],"--strict-wide-wrap")){api=LOAD_W;args[0]=UINT32_MAX;}
        if(vm)args[0]=BASE+0xe0000;
        if(no_provider)args[0]=text(NAME_TEXT,"kernel32.dll",0);else if(!vm)arena[ARENA-1]='k';
        assert(execute(0x401500+api*32,args,1,&v,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK);if(m)assert(fnv_windows_modules_destroy(m)==FNV_CPU_OK);
        assert(fnv_windows_destroy(process)==FNV_CPU_OK);if(vm)assert(!fnv_vm_destroy(vm).error);
        assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    assert(fnv_windows_modules_destroy(m)==FNV_CPU_BUSY);
    assert(call(GET_A,0,0)==BASE && call(GET_W,0,0)==BASE);
    uint32_t error_token=0;unsigned aliases=0;
    for(size_t i=0;i<count;++i)if(!strcmp(imports[i].name,"GetLastError")){
        uint32_t token=read32(BASE+imports[i].iat_rva);if(error_token)assert(token==error_token);error_token=token;aliases++;}
    assert(aliases==2 && error_token);
    uint32_t kernel=call(GET_A,text(NAME_TEXT,"kernel32.dll",0),0);
    assert(call(GET_PROC,kernel,text(PROC_TEXT,"GetLastError",0))==error_token);
    call(SET_ERROR,0x55fa,0);assert(call(GET_ERROR_ALIAS,0,0)==0x55fa && indirect(error_token)==0x55fa);
    struct fnv_windows_module_stats snapshot;assert(fnv_windows_modules_stats(m,&snapshot)==FNV_CPU_OK);
    assert(snapshot.pinned_modules==4 && snapshot.native_exports==builtins+9 && !snapshot.load_calls && !snapshot.dynamic_references);
    assert(call(GET_A,text(NAME_TEXT,"falloutnv.exe",0),0)==BASE);
    HMODULE native=GetModuleHandleW(L"kernel32.dll");assert(native);
    const char *names[]={"kernel32.dll","KERNEL32.DLL","kernel32","kernel32.dll."};
    unsigned checks=0;
    for(unsigned wide=0;wide<2;++wide)for(unsigned i=0;i<4;++i){
        uint32_t h=call(wide?GET_W:GET_A,text(NAME_TEXT,names[i],wide),0);assert(h);
        call(SET_ERROR,0x9876,0);assert(call(wide?LOAD_W:LOAD_A,NAME_TEXT,0)==h && call(GET_ERROR,0,0)==0x9876);
        assert(GetModuleHandleA(names[i])==native);checks++;
        assert(call(FREE,h,0));
        const char *procs[]={"GetCurrentThreadId","GetCurrentProcessId","getcurrentthreadid","NoSuchFunction_f09d1"};
        for(unsigned j=0;j<4;++j){uint32_t token=call(GET_PROC,h,text(PROC_TEXT,procs[j],0));FARPROC host=GetProcAddress(native,procs[j]);assert((token!=0)==(host!=NULL));checks++;
            if(j<2)assert(indirect(token)==(j?11:22));else assert(call(GET_ERROR,0,0)==ERROR_PROC_NOT_FOUND);}
    }
    assert(!call(LOAD_A,text(NAME_TEXT,"missing_f09d1.dll",0),0) && call(GET_ERROR,0,0)==ERROR_MOD_NOT_FOUND);
    assert(!call(GET_A,NAME_TEXT,0) && call(GET_ERROR,0,0)==ERROR_MOD_NOT_FOUND);
    assert(!call(LOAD_A,text(NAME_TEXT,"dir/fixture.dll",0),0));
    uint32_t h=call(LOAD_A,text(NAME_TEXT,"fixture.dll",0),0);assert(h);
    uint32_t token=call(GET_PROC,h,text(PROC_TEXT,"Value",0));assert(token && indirect(token)==0x1234abcd);
    uint32_t ordinal=call(GET_PROC,h,7);assert(ordinal==token && indirect(ordinal)==0x1234abcd);
    assert(!call(GET_PROC,h,8) && !call(GET_PROC,h,0));
    assert(!call(GET_PROC,h,text(PROC_TEXT,"value",0)));assert(call(FREE,h,0));
    assert(call(GET_A,NAME_TEXT,0)==h && call(GET_PROC,h,text(PROC_TEXT,"Value",0))==token); /* Pinned. */
    assert(!call(FREE,h|1,0) && call(GET_ERROR,0,0)==ERROR_INVALID_HANDLE);
    HANDLE threads[4];for(unsigned i=0;i<4;++i){threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==0);for(unsigned i=0;i<4;++i){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    assert(fnv_windows_modules_stats(m,&snapshot)==FNV_CPU_OK && snapshot.load_calls==2009 && !snapshot.dynamic_references);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_BUSY);
    assert(fnv_windows_modules_destroy(m)==FNV_CPU_OK);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && !call(GET_ERROR,0,0) && !call(GET_ERROR_ALIAS,0,0));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_name_checks\":%u,\"concurrent_cycles\":2000,\"indirect_native_calls\":true,\"ordinal_lookup\":true,\"canonical_iat_identity\":true,\"static_aliases\":2,\"static_import_survives_catalog_destroy\":true,\"snapshot_owned\":true,\"pinned_namespace\":true,\"native_engine_started\":false}\n",checks);return 0;
}
