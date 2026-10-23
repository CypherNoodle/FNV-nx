#include <windows.h>
#include "bink_sound.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define RECORD 0x480000u
static unsigned char *arena;
static uint32_t backend_result,backend_parameter,backend_calls;static int short_stack;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t get(uint32_t at){uint32_t v;memcpy(&v,arena+at-BASE,4);return v;}
static uint32_t sound_factory(void *context,uint32_t p){assert(context==&backend_result);backend_calls++;backend_parameter=p;return backend_result;}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+(short_stack?0x14040:0x1bf00);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT);return 0;}
    if(r)fprintf(stderr,"Bink bridge: %s\n",fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_bink_sound_config cfg={&backend_result,sound_factory};if(argc==3 && !strcmp(argv[2],"--strict-backend"))cfg.open_directsound=NULL;
    struct fnv_bink_sound *sound;assert(fnv_bink_sound_create(process,&cfg,&sound)==FNV_CPU_OK);
    struct fnv_bink_sound *duplicate=(void*)1;assert(fnv_bink_sound_create(process,&cfg,&duplicate)==FNV_CPU_BUSY && !duplicate);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_bink_sound_exports(&n);assert(n==2);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==4);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(fnv_bink_sound_destroy(sound)==FNV_CPU_BUSY);
    uint32_t args[]={0x401c00,0x12345678};
    if(argc==3){
        if(!strcmp(argv[2],"--strict-backend")){call(1,args,1,1);}
        else {if(!strcmp(argv[2],"--strict-callback"))args[0]=0x401e00;
            else if(!strcmp(argv[2],"--strict-stack"))short_stack=1;
            else if(!strcmp(argv[2],"--strict-recursion"))args[0]=0x401c40;
            else if(!strcmp(argv[2],"--strict-abi"))args[0]=0x401c80;
            else assert(0);
            call(0,args,2,1);}
    }else{
        struct fnv_bink_sound_state selected;assert(fnv_bink_sound_state(sound,&selected)==FNV_CPU_OK && !selected.open);
        uint32_t model=0,model_parameter=0,seed=0x781204u;put(RECORD+4,0);
        for(unsigned i=0;i<2000;i++){
            seed=seed*1664525u+1013904223u;args[1]=seed;uint32_t result=(seed&16)?0x401d00:0;put(RECORD+8,result);
            uint32_t error[]={0x4321};call(3,error,1,0);assert(call(0,args,2,0)==(model!=0 || result!=0));assert(call(2,NULL,0,0)==0x4321);
            if(result){model=result;model_parameter=seed;}
            assert(get(RECORD)==seed && get(RECORD+4)==i+1);
            assert(fnv_bink_sound_state(sound,&selected)==FNV_CPU_OK && selected.open==model && selected.parameter==model_parameter && selected.factory==(model?args[0]:0));
        }
        args[0]=0;assert(!call(0,args,2,0));assert(fnv_bink_sound_state(sound,&selected)==FNV_CPU_OK && !selected.factory && !selected.open && !selected.parameter);
        args[0]=fnv_cpu_import_token(e[1].function);assert(args[0]);
        for(unsigned i=0;i<64;i++){
            backend_result=i&1?0x401d20:0;args[1]=i;uint32_t before=backend_calls;assert(call(0,args,2,0)==(i!=0));assert(backend_calls==before+1 && backend_parameter==i);
            assert(fnv_bink_sound_state(sound,&selected)==FNV_CPU_OK && selected.open==(i?0x401d20:0));
        }
        args[0]=0;assert(!call(0,args,2,0));assert(fnv_bink_sound_state(sound,&selected)==FNV_CPU_OK && !selected.open);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_bink_sound_destroy(sound)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"translated_sound_callbacks\":2064,\"callback_failure_retains_selection\":true,\"sound_decoded\":false,\"engine_started\":false}");return 0;
}
