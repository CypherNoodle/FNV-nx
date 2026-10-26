#define main fnv_existing_matrix_main
#include "windows_d3dx_host.c"
#undef main
#include <math.h>
int main(int argc,char **argv){
    assert(argc>=2 && argc<=4);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_d3dx_exports(&n);assert(n==13);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t args[4]={RECORD,0,0,0};memset(arena+RECORD-BASE-16,0xa5,96);
    if(argc==3){
        if(!strcmp(argv[2],"--strict-null"))args[0]=0;
        else if(!strcmp(argv[2],"--strict-range"))args[0]=BASE+BYTES-32;
        else if(!strcmp(argv[2],"--strict-nan"))args[1]=0x7fc00000;
        else if(!strcmp(argv[2],"--strict-denormal"))args[2]=1;
        else if(!strcmp(argv[2],"--strict-angle")){float f=65;memcpy(args+3,&f,4);}
        else {assert(!strcmp(argv[2],"--strict-rounding"));assert(!fesetround(FE_DOWNWARD));}
        call(0,args,4,1);assert(!fesetround(FE_TONEAREST));
        for(unsigned i=0;i<96;i++)assert(arena[RECORD-BASE-16+i]==0xa5);
    }else{
        FILE *oracle=NULL;uint32_t count=5000;
        if(argc==4){assert(!strcmp(argv[2],"--oracle"));oracle=fopen(argv[3],"rb");assert(oracle && fread(&count,4,1,oracle)==1 && count==5000);}
        HMODULE dll=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(dll);
        typedef float *(WINAPI *rotation_fn)(float*,float,float,float);FARPROC address=GetProcAddress(dll,"D3DXMatrixRotationYawPitchRoll");rotation_fn rotation;memcpy(&rotation,&address,sizeof(rotation));assert(rotation);
        float worst=0;
        for(unsigned j=0;j<count;j++){
            float angles[3],expected[16],actual[16];
            if(oracle){assert(fread(angles,12,1,oracle)==1 && fread(expected,64,1,oracle)==1);}
            else{
                for(unsigned i=0;i<3;i++)angles[i]=number()*0.25f;
                if(j<4){angles[0]=angles[1]=angles[2]=0;if(j)angles[j-1]=1.57079632679f;}
                if(j==4)angles[0]=angles[1]=angles[2]=64;
                assert(rotation(expected,angles[0],angles[1],angles[2])==expected);
            }
            for(unsigned i=0;i<3;i++)memcpy(args+1+i,angles+i,4);
            uint32_t error[]={0x4234};call(2,error,1,0);assert(call(0,args,4,0)==RECORD && call(1,NULL,0,0)==0x4234);
            memcpy(actual,arena+RECORD-BASE,64);
            for(unsigned i=0;i<16;i++){float delta=fabsf(actual[i]-expected[i]);if(delta>worst)worst=delta;assert(isfinite(actual[i]) && delta<=0.000002f);}
            for(unsigned i=0;i<16;i++){assert(arena[RECORD-BASE-16+i]==0xa5);assert(arena[RECORD-BASE+64+i]==0xa5);}
        }
        if(oracle)assert(fgetc(oracle)==EOF && !ferror(oracle) && !fclose(oracle));
        FreeLibrary(dll);printf("{\"status\":\"PASS\",\"rotation_matrices\":%u,\"max_absolute_error\":%.9g,\"hardware_tested\":false}\n",count,(double)worst);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3)puts("strict PASS");return 0;
}
