#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define NAME_AT (BASE+0x9000)
#define OUTPUT (BASE+0xa000)
#define PART (BASE+0xb000)
static unsigned char *arena;
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t get32(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;++i)v|=(uint32_t)arena[at-BASE+i]<<(i*8);return v;}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *args,uint32_t *value,struct fnv_cpu_fault *fault){
    static const unsigned counts[]={4,4,0,1};struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(stack,0x401500);
    for(unsigned i=0;i<counts[api];++i)put32(stack+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+counts[api]*4);*value=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    uint32_t args[]={a,b,c,d},v=0;struct fnv_cpu_fault fault;enum fnv_cpu_result r=execute(api,args,&v,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"full path: %s\n",fault.text);assert(r==FNV_CPU_OK);return v;
}
static void path(const char *s,int wide){for(size_t i=0;i<=strlen(s);++i){arena[NAME_AT-BASE+i*(wide?2:1)]=(unsigned char)s[i];if(wide)arena[NAME_AT-BASE+i*2+1]=0;}}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    char root[2048];strcpy(root,argv[1]);char *end=strrchr(root,'\\'),*slash=strrchr(root,'/');if(slash && (!end || slash>end))end=slash;assert(end);*end=0;
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=4;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==4);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    unsigned checks=0;
    if(argc==3){
        path("Data/movie.bik",0);uint32_t args[]={NAME_AT,512,OUTPUT,PART},v=0;struct fnv_cpu_fault fault;
        if(!strcmp(argv[2],"--strict-output"))args[2]=BASE+ARENA-1;
        else if(!strcmp(argv[2],"--strict-part"))args[3]=BASE+ARENA-1;
        else if(!strcmp(argv[2],"--strict-alias"))args[3]=OUTPUT+4;
        else if(!strcmp(argv[2],"--strict-input"))args[0]=0;
        else assert(0);
        memset(arena+OUTPUT-BASE,0xa5,1024);assert(execute(0,args,&v,&fault)!=FNV_CPU_OK);
        for(unsigned i=0;i<1024;++i)assert(arena[OUTPUT-BASE+i]==0xa5);
        puts("strict PASS");
    }else{
        const char *names[]={"Data\\Video\\Fallout INTRO Vsk.bik","Data/./Textures/../Video/movie.bik",".",".\\","folder/..","folder/../","missing/file.bin","Data//Video///movie.bik","C:\\FNV-nx\\Data\\movie.bik","C:\\Users\\FNV\\Documents\\My Games\\FalloutNV\\Fallout.ini","C:\\Users\\FNV\\Documents\\..\\"};
        for(unsigned wide=0;wide<2;++wide)for(unsigned n=0;n<sizeof(names)/sizeof(*names);++n){
            char absolute[1024];if(names[n][1]==':')strcpy(absolute,names[n]);else snprintf(absolute,sizeof absolute,"C:\\FNV-nx\\%s",names[n]);
            WCHAR input[1024];for(size_t j=0;j<=strlen(absolute);++j)input[j]=(unsigned char)absolute[j];
            unsigned char native[2048];char *pa;WCHAR *pw;
            DWORD length=wide?GetFullPathNameW(input,1024,(WCHAR *)native,&pw):GetFullPathNameA(absolute,1024,(char *)native,&pa);assert(length && length<512);
            unsigned capacities[]={0,1,length,length+1,512,UINT32_MAX};
            for(unsigned alias=0;alias<2;++alias)for(unsigned k=0;k<6;++k){
                unsigned width=wide?2:1;uint32_t out=alias?NAME_AT:OUTPUT;path(names[n],wide);unsigned char before[1024];
                if(!alias)memset(arena+out-BASE,0xa5,1024);memcpy(before,arena+out-BASE,1024);put32(PART,0xdeadbeef);
                call(3,0x4234,0,0,0);uint32_t v=call(wide,NAME_AT,capacities[k],out,PART);assert(call(2,0,0,0,0)==0x4234);
                if(capacities[k]<=length){assert(v==length+1 && get32(PART)==0xdeadbeef && !memcmp(before,arena+out-BASE,1024));}
                else {assert(v==length && !memcmp(native,arena+out-BASE,(length+1)*width));
                    uint32_t offset=wide?(pw?(uint32_t)((unsigned char *)pw-native):UINT32_MAX):(pa?(uint32_t)((unsigned char *)pa-native):UINT32_MAX);
                    assert(get32(PART)==(offset==UINT32_MAX?0:out+offset));assert(!memcmp(before+(length+1)*width,arena+out-BASE+(length+1)*width,1024-(length+1)*width));}
                checks++;
            }
            path(names[n],wide);assert(call(wide,NAME_AT,512,OUTPUT,0)==length);checks++;
        }
        const char *bad[]={"","../outside","C:\\Windows\\file","Data/*.bik","Data/quote\".bik"};const unsigned errors[]={123,5,50,123,123};
        for(unsigned w=0;w<2;++w)for(unsigned i=0;i<5;++i){path(bad[i],w);memset(arena+OUTPUT-BASE,0xa5,1024);put32(PART,0xdeadbeef);
            assert(!call(w,NAME_AT,512,OUTPUT,PART) && call(2,0,0,0,0)==errors[i]);assert(get32(PART)==0xdeadbeef);
            for(unsigned j=0;j<1024;++j)assert(arena[OUTPUT-BASE+j]==0xa5);checks++;}
        printf("{\"status\":\"PASS\",\"path_checks\":%u,\"windows_oracle\":true,\"host_io_required\":false}\n",checks);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));return 0;
}
