#define main fnv_existing_matrix_main
#include "windows_d3dx_host.c"
#undef main
#include <math.h>
static const char *names[8]={"D3DXMatrixTranspose","D3DXMatrixInverse","D3DXVec4Transform","D3DXVec3TransformCoord","D3DXVec3TransformNormal","D3DXPlaneTransform","D3DXVec3Normalize","D3DXPlaneNormalize"};
static unsigned count_for(unsigned api){return api<2?16:api==2 || api==5 || api==7?4:3;}
static unsigned arguments_for(unsigned api){return api==0 || api>=6?2:3;}
static void reference(FARPROC address,unsigned api,float *out,float *det,const float *v,const float *m,uint32_t *ok){
    typedef float *(WINAPI *two_fn)(float*,const float*);
    typedef float *(WINAPI *three_fn)(float*,const float*,const float*);
    typedef float *(WINAPI *inverse_fn)(float*,float*,const float*);
    float *result;
    if(api==0 || api>=6){two_fn fn;memcpy(&fn,&address,sizeof(fn));result=fn(out,api==0?m:v);}
    else if(api==1){inverse_fn fn;memcpy(&fn,&address,sizeof(fn));result=fn(out,det,m);}
    else{three_fn fn;memcpy(&fn,&address,sizeof(fn));result=fn(out,v,m);}
    *ok=result!=NULL;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3 || argc==4);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_d3dx_exports(&n);assert(n==13);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==10);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    float m[16]={0},v[4]={1,2,3,4};for(unsigned i=0;i<4;i++)m[i*5]=1;
    uint32_t args[3]={RECORD,RECORD+128,RECORD+256};
    if(argc==3){
        unsigned api=6;memset(arena+RECORD-BASE,0xa5,64);memcpy(arena+RECORD+128-BASE,v,16);memcpy(arena+RECORD+256-BASE,m,64);
        if(!strcmp(argv[2],"--strict-nan"))put(RECORD+128,0x7fc00000);
        else if(!strcmp(argv[2],"--strict-denormal"))put(RECORD+128,1);
        else if(!strcmp(argv[2],"--strict-range"))args[0]=BASE+BYTES-8;
        else if(!strcmp(argv[2],"--strict-null"))args[1]=0;
        else if(!strcmp(argv[2],"--strict-overflow")){v[0]=FLT_MAX;memcpy(arena+RECORD+128-BASE,v,16);}
        else if(!strcmp(argv[2],"--strict-plane")){api=7;memset(arena+RECORD+128-BASE,0,12);put(RECORD+140,0x3f800000);}
        else if(!strcmp(argv[2],"--strict-coord")){api=3;m[15]=0;memcpy(arena+RECORD+256-BASE,m,64);}
        else if(!strcmp(argv[2],"--strict-inverse")){api=1;m[0]=1e-13f;memcpy(arena+RECORD+256-BASE,m,64);args[1]=RECORD+512;}
        else if(!strcmp(argv[2],"--strict-determinant-alias")){api=1;args[1]=RECORD+4;}
        else {assert(!strcmp(argv[2],"--strict-rounding"));assert(!fesetround(FE_DOWNWARD));}
        call(api,args,arguments_for(api),1);assert(!fesetround(FE_TONEAREST));for(unsigned i=0;i<64;i++)assert(arena[RECORD-BASE+i]==0xa5);
    }else{
        FILE *oracle=NULL;uint32_t cases=1000;if(argc==4){assert(!strcmp(argv[2],"--oracle"));oracle=fopen(argv[3],"rb");assert(oracle && fread(&cases,4,1,oracle)==1 && cases==1000);}
        HMODULE dll=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(dll);FARPROC functions[8];for(unsigned i=0;i<8;i++){functions[i]=GetProcAddress(dll,names[i]);assert(functions[i]);}
        float worst=0,worst_scaled=0;unsigned singular=0;
        for(unsigned j=0;j<cases;j++){
            if(oracle){assert(fread(m,64,1,oracle)==1 && fread(v,16,1,oracle)==1);}
            else{
                for(unsigned i=0;i<16;i++)m[i]=number()/512;
                for(unsigned i=0;i<4;i++){m[i*5]+=4;v[i]=number()/64;}
                if(j==0)memset(v,0,sizeof(v));
                if(j==1){float projection[16]={1.5f,0,0,0,0,2,0,0,0,0,1000.0f/999,1,0,0,-1000.0f/999,0};memcpy(m,projection,64);v[2]=2;}
                if(j==2){float affine[16]={2,0,0,0,0,-3,0,0,0,0,4,0,10000,20000,-30000,1};memcpy(m,affine,64);}
            }
            for(unsigned api=0;api<8;api++){
                float matrix[16],expected[16],det=99;memcpy(matrix,m,64);uint32_t ok=0;
                if(api==1 && j%97==0)memset(matrix,0,64);
                memset(expected,0xa5,sizeof(expected));
                if(oracle){assert(fread(expected,64,1,oracle)==1 && fread(&det,4,1,oracle)==1 && fread(&ok,4,1,oracle)==1);}
                else reference(functions[api],api,expected,&det,v,matrix,&ok);
                if(!ok)singular++;
                for(unsigned alias=0;alias<4;alias++){
                    memset(arena+RECORD-BASE-32,0xa5,1024);memcpy(arena+RECORD+128-BASE,v,16);memcpy(arena+RECORD+256-BASE,matrix,64);
                    uint32_t source=api<2?RECORD+256:RECORD+128;
                    args[0]=alias==0?RECORD:alias==1?source:alias==2?source+4:api<2?source-4:RECORD+260;
                    args[1]=api==1?(alias&1?0:RECORD+512):source;args[2]=api==1?source:RECORD+256;
                    if(api==1 && args[1]){float initial_det=99;memcpy(arena+args[1]-BASE,&initial_det,4);}
                    unsigned bytes=count_for(api)*4;unsigned char before[64],low[16],high[16];
                    memcpy(before,arena+args[0]-BASE,bytes);memcpy(low,arena+args[0]-BASE-16,16);memcpy(high,arena+args[0]-BASE+bytes,16);
                    uint32_t error[]={0x4234};call(9,error,1,0);assert(call(api,args,arguments_for(api),0)==(ok?args[0]:0));assert(call(8,NULL,0,0)==0x4234);
                    assert(!memcmp(low,arena+args[0]-BASE-16,16) && !memcmp(high,arena+args[0]-BASE+bytes,16));
                    if(!ok)assert(!memcmp(before,arena+args[0]-BASE,bytes));
                    else if(api==0)assert(!memcmp(expected,arena+args[0]-BASE,64));
                    else{
                        float actual[16];memcpy(actual,arena+args[0]-BASE,bytes);
                        for(unsigned i=0;i<count_for(api);i++){float delta=fabsf(actual[i]-expected[i]);if(delta>worst)worst=delta;
                            float scaled=delta/(1+fabsf(expected[i]));if(scaled>worst_scaled)worst_scaled=scaled;
                            if(!(isfinite(actual[i]) && delta<=0.00002f*(1+fabsf(expected[i]))))fprintf(stderr,"math mismatch api=%u case=%u i=%u want=%g got=%g\n",api,j,i,(double)expected[i],(double)actual[i]);
                            assert(isfinite(actual[i]) && delta<=0.00002f*(1+fabsf(expected[i])));}
                    }
                    if(api==1 && args[1]){float actual,wanted=(!ok && !oracle)?99:det;memcpy(&actual,arena+RECORD+512-BASE,4);assert(fabsf(actual-wanted)<=0.00002f*(1+fabsf(wanted)));}
                    if(api==1 && !args[1])for(unsigned i=0;i<4;i++)assert(arena[RECORD+512-BASE+i]==0xa5);
                }
            }
        }
        if(oracle)assert(fgetc(oracle)==EOF && !ferror(oracle) && !fclose(oracle));
        /* Transposition must preserve arbitrary encodings and ignore rounding. */
        assert(!fesetround(FE_DOWNWARD));
        for(unsigned j=0;j<2000;j++){
            uint32_t bits[16],wanted[16];for(unsigned i=0;i<16;i++){random_state=random_state*1664525u+1013904223u;bits[i]=random_state;}
            if(!j){const uint32_t special[8]={0,0x80000000u,0x7fc00001u,0x7f800001u,1,0xff800000u,0x7f800000u,0x00800000u};memcpy(bits,special,sizeof(special));}
            uint32_t ok;reference(functions[0],0,(float*)wanted,NULL,NULL,(const float*)bits,&ok);assert(ok);
            memcpy(arena+RECORD-BASE,bits,64);uint32_t two[]={RECORD,RECORD};assert(call(0,two,2,0)==RECORD && !memcmp(arena+RECORD-BASE,wanted,64));
        }
        assert(!fesetround(FE_TONEAREST));FreeLibrary(dll);
        printf("{\"status\":\"PASS\",\"operations\":8,\"cases\":%u,\"alias_modes\":4,\"singular_matrices\":%u,\"arbitrary_transposes\":2000,\"max_absolute_error\":%.9g,\"max_scaled_error\":%.9g,\"hardware_tested\":false}\n",cases,singular,(double)worst,(double)worst_scaled);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));if(argc==3)puts("strict PASS");return 0;
}
