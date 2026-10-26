#include <windows.h>
#include "windows_thread.h"
#include "windows_d3dx.h"
#include <fenv.h>
#include <float.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define RECORD (BASE+0x80000)
#define NAME_ADDRESS (BASE+0x81000)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t random_state=0x71acdefu;
static float number(void){random_state=random_state*1664525u+1013904223u;return (float)((int32_t)(random_state>>24)-128);}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3 || argc==4);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_d3dx_exports(&n);assert(n==13);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==5);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && !*fnv_windows_current_debug_name());
    uint32_t args[]={RECORD,RECORD+128,RECORD+256};float a[16],b[16],expected[16];
    for(unsigned i=0;i<16;i++){a[i]=(float)(i+1);b[i]=(float)(i==0 || i==5 || i==10 || i==15);}
    memcpy(arena+args[1]-BASE,a,64);memcpy(arena+args[2]-BASE,b,64);memset(arena+RECORD-BASE,0xa5,64);
    int transpose=(argc==3 && !strcmp(argv[2],"--transpose")) || (argc==4 && !strcmp(argv[2],"--x86-transpose"));
    if(argc==3 && !transpose){
        if(!strcmp(argv[2],"--strict-perf")){uint32_t options[]={2};call(3,options,1,1);}
        else {
        if(!strcmp(argv[2],"--strict-transpose-range"))args[0]=BASE+BYTES-32;
        else if(!strcmp(argv[2],"--strict-null"))args[1]=0;
        else if(!strcmp(argv[2],"--strict-range"))args[0]=BASE+BYTES-32;
        else if(!strcmp(argv[2],"--strict-nan"))put(args[1],0x7fc00000u);
        else if(!strcmp(argv[2],"--strict-denormal"))put(args[1],1);
        else if(!strcmp(argv[2],"--strict-overflow")){a[0]=b[0]=FLT_MAX;memcpy(arena+args[1]-BASE,a,64);memcpy(arena+args[2]-BASE,b,64);}
        else assert(!fesetround(FE_DOWNWARD));
        call(!strcmp(argv[2],"--strict-transpose-range")?4:0,args,3,1);assert(!fesetround(FE_TONEAREST));
        }
        for(unsigned i=0;i<64;i++)assert(arena[RECORD-BASE+i]==0xa5);
    }else{
        uint32_t options[]={0},error[]={0x4234};call(2,error,1,0);call(3,options,1,0);options[0]=1;call(3,options,1,0);assert(call(1,NULL,0,0)==0x4234);
        HMODULE dll=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(dll);
        typedef float *(WINAPI *multiply_fn)(float *,const float *,const float *);FARPROC proc=GetProcAddress(dll,transpose?"D3DXMatrixMultiplyTranspose":"D3DXMatrixMultiply");multiply_fn multiply;memcpy(&multiply,&proc,sizeof(multiply));assert(multiply);
        FILE *oracle_file=NULL;if(argc==4){assert(!strcmp(argv[2],"--x86-oracle") || !strcmp(argv[2],"--x86-transpose"));oracle_file=fopen(argv[3],"rb");uint32_t count=0;assert(oracle_file && fread(&count,4,1,oracle_file)==1 && count==10000);}
        for(unsigned ncase=0;ncase<10000;ncase++){
            if(oracle_file){assert(fread(a,64,1,oracle_file)==1 && fread(b,64,1,oracle_file)==1 && fread(expected,64,1,oracle_file)==1);}
            else {for(unsigned i=0;i<16;i++){a[i]=number();b[i]=number();}assert(multiply(expected,a,b)==expected);}
            memcpy(arena+args[1]-BASE,a,64);memcpy(arena+args[2]-BASE,b,64);
            args[0]=ncase%4==0?RECORD:ncase%4==1?args[1]:ncase%4==2?args[2]:args[1]+4;
            uint32_t error[]={0x4234};call(2,error,1,0);assert(call(transpose?4:0,args,3,0)==args[0] && call(1,NULL,0,0)==0x4234);
            assert(!memcmp(arena+args[0]-BASE,expected,64));
        }
        if(oracle_file)assert(fgetc(oracle_file)==EOF && !ferror(oracle_file) && !fclose(oracle_file));
        FreeLibrary(dll);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3 && !transpose){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"matrix_comparisons\":10000,\"alias_modes\":4,\"engine_started\":false}");return 0;
}
