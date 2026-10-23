#define main fnv_existing_files_main
#include "windows_files_host.c"
#undef main
static const unsigned directory_counts[]={2,2,7,1,5,1,0,1};
static uint32_t directory_call(unsigned api,const uint32_t *args,struct fnv_cpu_fault *fault,enum fnv_cpu_result *result){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0x401500);
    for(unsigned i=0;i<directory_counts[api];++i)put32(s.gpr[4]+4+i*4,args[i]);
    *result=fnv_cpu_execute(0x401500+api*64,fault);
    if(*result!=FNV_CPU_OK)return 0;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+directory_counts[api]*4);return s.gpr[0];
}
static uint32_t dc(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g){
    uint32_t args[]={a,b,c,d,e,f,g};struct fnv_cpu_fault fault;enum fnv_cpu_result result;
    uint32_t value=directory_call(api,args,&fault,&result);if(result!=FNV_CPU_OK)fprintf(stderr,"directory API %u: %s\n",api,fault.text);
    assert(result==FNV_CPU_OK);return value;
}
#define D(api,a,b) dc(api,a,b,0,0,0,0,0)
int main(int argc,char **argv){
    assert(argc==2 || argc==3);char root[2048];strcpy(root,argv[1]);char *last=strrchr(root,'\\'),*slash=strrchr(root,'/');
    if(slash && (!last || slash>last))last=slash;
    assert(last);*last=0;assert(strlen(root)+32<sizeof(root));strcat(root,argc==3?"/cwd-strict":"/cwd-root");fixture(root,0);
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=4;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==8);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={100,BASE+ARENA-2};struct fnv_cpu_fault fault;enum fnv_cpu_result result;
        (void)directory_call(1,args,&fault,&result);assert(result==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    const char virtual_root[]="C:\\FNV-nx";unsigned length=(unsigned)strlen(virtual_root),checks=0;
    char native_root[32768];DWORD native_length=GetCurrentDirectoryA(sizeof(native_root),native_root);assert(native_length && native_length<sizeof(native_root));
    for(unsigned wide=0;wide<2;++wide)for(unsigned mode=0;mode<6;++mode){
        unsigned capacity=mode==0?0:mode==1?1:mode==2?length-1:mode==3?length:mode==4?length+1:500;
        DWORD native_capacity=mode==0?0:mode==1?1:mode==2?native_length-1:mode==3?native_length:mode==4?native_length+1:32768;
        memset(arena+OUTPUT-BASE,0xa5,64);D(7,0x8765,0);SetLastError(0x8765);
        unsigned char native_buffer[65536];memset(native_buffer,0xa5,sizeof(native_buffer));
        DWORD wanted=wide?GetCurrentDirectoryW(native_capacity,(WCHAR *)native_buffer):GetCurrentDirectoryA(native_capacity,(char *)native_buffer);
        DWORD error=GetLastError();uint32_t got=D(wide,capacity,mode==0?0:OUTPUT);
        assert(got==(capacity<=length?length+1:length));assert(wanted==(native_capacity<=native_length?native_length+1:native_length));
        assert(D(6,0,0)==error);
        if(capacity<=length){for(unsigned i=0;i<64;++i)assert(arena[OUTPUT-BASE+i]==0xa5);assert(native_buffer[0]==0xa5);}
        else {for(unsigned i=0;i<=length;++i){assert(arena[OUTPUT-BASE+i*(wide?2:1)]==(unsigned char)virtual_root[i]);if(wide)assert(!arena[OUTPUT-BASE+i*2+1]);}
            assert(arena[OUTPUT-BASE+(length+1)*(wide?2:1)]==0xa5);}
        checks++;
    }
    const char *aliases[]={"Data/fixture.bin","C:\\FNV-nx\\Data\\fixture.bin","c:/fnv-NX/Data/fixture.bin","C:\\FNV-nx\\Data\\..\\Data\\fixture.bin"};
    for(unsigned i=0;i<4;++i){uint32_t h=dc(2,path(aliases[i],0),GENERIC_READ,1,0,3,0x80,0);assert(h!=UINT32_MAX);
        assert(dc(4,h,BUFFER,16,OUTPUT,0,0,0) && read32(OUTPUT)==16 && read32(BUFFER)==0 && read32(BUFFER+12)==3);assert(D(5,h,0));checks++;}
    const char *denied[]={"C:\\FNV-nx2\\Data\\fixture.bin","C:\\FNV-nx\\..\\escape","D:\\FNV-nx\\Data\\fixture.bin","\\\\server\\share\\x"};
    for(unsigned i=0;i<4;++i){assert(dc(2,path(denied[i],0),GENERIC_READ,1,0,3,0x80,0)==UINT32_MAX);assert(D(6,0,0)==(i==1?5:50));}
    uint32_t attributes=D(3,path(virtual_root,0),0);
    assert(attributes!=UINT32_MAX && (attributes&FILE_ATTRIBUTE_DIRECTORY));
    assert(dc(2,path("C:\\FNV-nx\\UserData\\forbidden.bin",0),GENERIC_WRITE,1,0,CREATE_ALWAYS,0x80,0)==UINT32_MAX && D(6,0,0)==50);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"directory_and_alias_checks\":%u,\"sandbox_negatives\":5,\"native_engine_started\":false}\n",checks);return 0;
}
