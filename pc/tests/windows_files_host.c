#include <windows.h>
#include <winioctl.h>
#include "windows_thread.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define NAME_AT (BASE+0x9000)
#define BUFFER (BASE+0x80000)
#define OUTPUT (BASE+0xA000)
enum {OPEN_A,OPEN_W,READ,FILE_SIZE,SIZE_EX,SEEK,SEEK_EX,CLOSE,GET_ERROR,SET_ERROR,CREATE_EVENT};
static const unsigned counts[]={7,7,5,2,2,4,5,1,0,1,4};
static unsigned char *arena;static struct fnv_windows_process *process;
static uint32_t permissions(void *context,void *at,uint32_t bytes,uint32_t protection){
    (void)context;DWORD old;return VirtualProtect(at,bytes,protection==3?PAGE_READWRITE:protection==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();}
static struct fnv_vm_permissions vm_backend={NULL,permissions};
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static struct fnv_windows_thread_config config(unsigned i){struct fnv_windows_thread_config t={0};t.thread_id=22+i;
    t.teb_address=BASE+0x10000+i*0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *a,uint32_t *v,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(stack,0x401500);for(unsigned i=0;i<counts[api];++i)put32(stack+4+i*4,a[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+counts[api]*4);*v=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,uint32_t f,uint32_t g){
    uint32_t args[]={a,b,c,d,e,f,g},v=0;struct fnv_cpu_fault fault;enum fnv_cpu_result r=execute(api,args,&v,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"file API %u: %s\n",api,fault.text);assert(r==FNV_CPU_OK);return v;
}
#define C(api,a,b,c,d,e) call(api,a,b,c,d,e,0,0)
static uint32_t path(const char *s,int wide){size_t n=strlen(s);assert(n<512);
    for(size_t i=0;i<=n;++i){arena[NAME_AT-BASE+i*(wide?2:1)]=(unsigned char)s[i];if(wide)arena[NAME_AT-BASE+i*2+1]=0;}return NAME_AT;}
static uint32_t open_file(const char *s,int wide,uint32_t share){return call(wide?OPEN_W:OPEN_A,path(s,wide),GENERIC_READ,share,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);}
static void fixture(const char *root,int large){
    assert(CreateDirectoryA(root,NULL));char data[2048];snprintf(data,sizeof(data),"%s/Data",root);assert(CreateDirectoryA(data,NULL));
    char full[2048];snprintf(full,sizeof(full),"%s/Data/fixture.bin",root);
    HANDLE f=CreateFileA(full,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(f!=INVALID_HANDLE_VALUE);
    uint32_t values[2048];for(unsigned i=0;i<2048;++i)values[i]=i;DWORD n;
    assert(WriteFile(f,values,sizeof(values),&n,NULL) && n==sizeof(values) && CloseHandle(f));
    if(large){snprintf(full,sizeof(full),"%s/Data/large.bin",root);f=CreateFileA(full,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(f!=INVALID_HANDLE_VALUE);
        DWORD returned;assert(DeviceIoControl(f,FSCTL_SET_SPARSE,NULL,0,NULL,0,&returned,NULL));
        LARGE_INTEGER position;position.QuadPart=INT64_C(0x1fffffffe);assert(SetFilePointerEx(f,position,NULL,FILE_BEGIN));
        unsigned char last=0x7a;assert(WriteFile(f,&last,1,&n,NULL) && n==1 && CloseHandle(f));}
}
static volatile LONG seen[128];static uint32_t shared_handle;
static DWORD WINAPI reader(void *context){unsigned index=(unsigned)(uintptr_t)context;struct fnv_windows_thread_config t=config(index);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t buffer=BUFFER+index*4096,count=buffer+1024;
    for(;;){assert(C(READ,shared_handle,buffer,64,count,0));uint32_t n=read32(count);if(!n)break;assert(n==64);
        unsigned first=read32(buffer);assert(!(first%16) && first<2048);assert(InterlockedCompareExchange(&seen[first/16],1,0)==0);
        for(unsigned i=0;i<16;++i)assert(read32(buffer+i*4)==first+i);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static int audit_game(const char *fixture_path,const char *root){
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;
    assert(fnv_native_load_image(fixture_path,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=4;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    const char *names[]={"FalloutNV.exe","Data/FalloutNV.esm","Data/Fallout - Textures.bsa","Data/Fallout - Sound.bsa"};
    for(unsigned i=0;i<4;++i){char full[2048];assert(strlen(root)+strlen(names[i])+2<sizeof(full));snprintf(full,sizeof(full),"%s/%s",root,names[i]);
        HANDLE native=CreateFileA(full,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
        BY_HANDLE_FILE_INFORMATION before,after;assert(GetFileInformationByHandle(native,&before));
        uint32_t h=open_file(names[i],i&1,FILE_SHARE_READ);assert(h!=UINT32_MAX);LARGE_INTEGER size;assert(GetFileSizeEx(native,&size) && size.QuadPart>=4096);
        assert(C(SIZE_EX,h,OUTPUT,0,0,0) && read32(OUTPUT)==size.LowPart && read32(OUTPUT+4)==(uint32_t)size.HighPart);
        unsigned char expected[4096];DWORD got;
        assert(C(READ,h,BUFFER,4096,OUTPUT,0) && read32(OUTPUT)==4096 && ReadFile(native,expected,sizeof(expected),&got,NULL) && got==4096);
        assert(!memcmp(expected,arena+BUFFER-BASE,4096));
        LARGE_INTEGER tail;tail.QuadPart=-4096;assert(SetFilePointerEx(native,tail,NULL,FILE_END));
        assert(C(SEEK_EX,h,(uint32_t)-4096,UINT32_MAX,0,FILE_END));
        assert(C(READ,h,BUFFER,4096,OUTPUT,0) && read32(OUTPUT)==4096 && ReadFile(native,expected,sizeof(expected),&got,NULL) && got==4096);
        assert(!memcmp(expected,arena+BUFFER-BASE,4096));assert(C(CLOSE,h,0,0,0,0));
        assert(GetFileInformationByHandle(native,&after) && before.nFileSizeHigh==after.nFileSizeHigh && before.nFileSizeLow==after.nFileSizeLow &&
            !memcmp(&before.ftLastWriteTime,&after.ftLastWriteTime,sizeof(FILETIME)));assert(CloseHandle(native));
    }
    struct fnv_windows_file_stats s;assert(fnv_windows_file_stats(process,&s)==FNV_CPU_OK && s.opens==4 && s.reads==8 && s.read_bytes==32768 && !s.live_handles);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"real_game_files\":4,\"compared_bytes\":32768,\"size_and_write_time_unchanged\":true,\"game_code_executed\":false}");return 0;
}
int main(int argc,char **argv){
    if(argc==4 && !strcmp(argv[2],"--audit-root"))return audit_game(argv[1],argv[3]);
    assert(argc==2 || argc==3);char root[2048],oracle[2048];size_t n=strlen(argv[1]);assert(n<sizeof(root)-32);memcpy(root,argv[1],n+1);
    char *last=strrchr(root,'\\');char *forward=strrchr(root,'/');if(forward && (!last || forward>last))last=forward;assert(last);*last=0;
    snprintf(oracle,sizeof(oracle),"%s/oracle-root%s",root,argc==3?argv[2]:"");strcat(root,"/guest-root");if(argc==3)strcat(root,argv[2]);fixture(root,1);fixture(oracle,1);
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.kernel_handle_capacity=2;c.file_root=root;c.file_handle_capacity=4;c.file_generation_limit=3;
    struct fnv_guest_memory *vm=NULL;
    if(argc==3 && !strcmp(argv[2],"--strict-noaccess")){
        struct fnv_vm_config v={arena+0xe0000,BASE+0xe0000,65536,65536,&vm_backend,fnv_vm_eager_apply};assert(!fnv_vm_create(&v,&vm).error);
        assert(fnv_vm_allocate(vm,v.base,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_NOACCESS).value==v.base);c.memory=vm;
    }
    if(argc==3 && !strcmp(argv[2],"--strict-disabled")){c.file_root=NULL;c.file_handle_capacity=0;c.file_generation_limit=0;}
    if(argc==2){struct fnv_windows_config invalid=c;struct fnv_windows_process *bad=NULL;
        invalid.file_handle_capacity=4097;assert(fnv_windows_create(&invalid,&bad)==FNV_CPU_INVALID && !bad);
        invalid=c;invalid.file_root=NULL;assert(fnv_windows_create(&invalid,&bad)==FNV_CPU_INVALID && !bad);
        invalid=c;invalid.file_generation_limit=0x1000000;assert(fnv_windows_create(&invalid,&bad)==FNV_CPU_INVALID && !bad);
    }
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);memset(root,'x',strlen(root)); /* Root snapshot ownership. */
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==11);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={NAME_AT,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0},v;struct fnv_cpu_fault fault;unsigned api=OPEN_A;
        if(!strcmp(argv[2],"--strict-path")){args[0]=BASE+ARENA-1;arena[ARENA-1]='x';}
        else if(!strcmp(argv[2],"--strict-buffer")){api=READ;args[0]=open_file("Data/fixture.bin",0,FILE_SHARE_READ);args[1]=BASE+ARENA-1;args[2]=16;args[3]=OUTPUT;args[4]=0;}
        else if(vm){api=READ;args[0]=open_file("Data/fixture.bin",0,FILE_SHARE_READ);args[1]=BASE+0xe0000;args[2]=16;args[3]=OUTPUT;args[4]=0;}
        else path("Data/fixture.bin",0);
        assert(execute(api,args,&v,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        if(vm)assert(!fnv_vm_destroy(vm).error);assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    uint32_t h=open_file("Data\\fixture.bin",0,FILE_SHARE_READ);assert(h!=UINT32_MAX && !(h&3));
    uint32_t other=open_file("Data/fixture.bin",1,FILE_SHARE_READ);assert(other!=UINT32_MAX && other!=h);assert(C(CLOSE,other|3,0,0,0,0));
    char full[2048];snprintf(full,sizeof(full),"%s/Data/fixture.bin",oracle);HANDLE native=CreateFileA(full,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
    unsigned seed=0x49bc,checks=0;unsigned char expected[512];
    for(unsigned i=0;i<2000;++i){seed=seed*1664525u+1013904223u;unsigned op=(seed>>24)%5;uint32_t got,wanted;
        C(SET_ERROR,0x9876,0,0,0,0);put32(OUTPUT,0x12345678);put32(OUTPUT+4,0x87654321);DWORD error;
        if(op==0){unsigned bytes=(seed>>12)%512;got=C(READ,h,BUFFER,bytes,OUTPUT,0);DWORD count=0x12345678;SetLastError(0x9876);wanted=ReadFile(native,expected,bytes,&count,NULL)!=0;error=GetLastError();
            assert(read32(OUTPUT)==count && !memcmp(arena+BUFFER-BASE,expected,count));}
        else if(op==1){int32_t distance=(int32_t)((seed>>10)%20000)-5000;unsigned method=(seed>>5)%4;
            got=C(SEEK_EX,h,(uint32_t)distance,distance<0?UINT32_MAX:0,OUTPUT,method);LARGE_INTEGER d,out;d.QuadPart=distance;out.QuadPart=INT64_C(0x8765432112345678);
            SetLastError(0x9876);wanted=SetFilePointerEx(native,d,&out,method)!=0;error=GetLastError();assert(read32(OUTPUT)==out.LowPart && read32(OUTPUT+4)==(uint32_t)out.HighPart);}
        else if(op==2){got=C(SIZE_EX,h,OUTPUT,0,0,0);LARGE_INTEGER size;SetLastError(0x9876);wanted=GetFileSizeEx(native,&size)!=0;error=GetLastError();assert(read32(OUTPUT)==size.LowPart && read32(OUTPUT+4)==(uint32_t)size.HighPart);}
        else if(op==3){got=C(FILE_SIZE,h,OUTPUT,0,0,0);DWORD high=0x12345678;SetLastError(0x9876);wanted=GetFileSize(native,&high);error=GetLastError();assert(read32(OUTPUT)==high);}
        else {int32_t distance=(int32_t)((seed>>10)%20000)-5000;unsigned method=(seed>>5)%4;got=C(SEEK,h,(uint32_t)distance,0,method,0);
            SetLastError(0x9876);wanted=SetFilePointer(native,distance,NULL,method);error=GetLastError();}
        if(got!=wanted || C(GET_ERROR,0,0,0,0,0)!=error)fprintf(stderr,"oracle op=%u got=%u wanted=%u err=%u expected=%lu\n",op,got,wanted,C(GET_ERROR,0,0,0,0,0),error);
        assert(got==wanted && C(GET_ERROR,0,0,0,0,0)==error);checks++;
    }
    assert(C(SEEK_EX,h,0,0,0,FILE_BEGIN));shared_handle=h;HANDLE threads[4];
    for(unsigned i=0;i<4;++i){threads[i]=CreateThread(NULL,0,reader,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==0);for(unsigned i=0;i<4;++i){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    for(unsigned i=0;i<128;++i)assert(seen[i]==1);
    assert(C(CLOSE,h,0,0,0,0) && CloseHandle(native));assert(!C(CLOSE,h,0,0,0,0) && C(GET_ERROR,0,0,0,0,0)==6);
    h=open_file("Data/large.bin",0,1);assert(h!=UINT32_MAX);C(SET_ERROR,0x9876,0,0,0,0);
    assert(C(FILE_SIZE,h,OUTPUT,0,0,0)==UINT32_MAX && read32(OUTPUT)==1 && C(GET_ERROR,0,0,0,0,0)==0);
    assert(C(SIZE_EX,h,OUTPUT,0,0,0) && read32(OUTPUT)==UINT32_MAX && read32(OUTPUT+4)==1);
    put32(OUTPUT,1);assert(C(SEEK,h,0xfffffffe,OUTPUT,FILE_BEGIN,0)==0xfffffffe && read32(OUTPUT)==1);
    assert(C(READ,h,BUFFER,8,OUTPUT,0) && read32(OUTPUT)==1 && arena[BUFFER-BASE]==0x7a);
    assert(C(READ,h,BUFFER,8,OUTPUT,0) && !read32(OUTPUT));assert(C(CLOSE,h,0,0,0,0));
    h=open_file("Data/fixture.bin",0,0);assert(h!=UINT32_MAX);assert(open_file("Data/fixture.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==32);assert(C(CLOSE,h,0,0,0,0));
    assert(open_file("../escape.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==5);
    assert(open_file("Data/missing.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==2);
    assert(open_file("missing/fixture.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==3);
    h=open_file("Data/../Data/fixture.bin",0,1);assert(h!=UINT32_MAX);
    assert(C(READ,h,0,0,OUTPUT,0) && !read32(OUTPUT));
    assert(!C(READ,h,BUFFER,4,OUTPUT,OUTPUT+16) && !read32(OUTPUT) && C(GET_ERROR,0,0,0,0,0)==50);
    assert(C(CLOSE,h,0,0,0,0));
    assert(call(OPEN_A,path("Data/should-not-exist.bin",0),GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==50);
    uint32_t handles[4];for(unsigned i=0;i<4;++i){handles[i]=open_file("Data/fixture.bin",0,1);assert(handles[i]!=UINT32_MAX);}
    assert(open_file("Data/fixture.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==4);
    for(unsigned i=0;i<4;++i)assert(C(CLOSE,handles[i],0,0,0,0));
    for(unsigned i=0;i<12;++i){uint32_t next=open_file("Data/fixture.bin",0,1);if(next==UINT32_MAX)break;assert(C(CLOSE,next,0,0,0,0));}
    assert(open_file("Data/fixture.bin",0,1)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==4);
    uint32_t event=C(CREATE_EVENT,0,1,0,0,0);assert(event && C(CLOSE,event,0,0,0,0));
    struct fnv_windows_file_stats stats;assert(fnv_windows_file_stats(process,&stats)==FNV_CPU_OK && !stats.live_handles && stats.read_bytes && stats.reads && !stats.read_errors && stats.retired_slots==4 && stats.opens==12);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_differential_operations\":%u,\"shared_cursor_chunks\":128,\"large_sparse_size\":8589934591,\"root_snapshot_owned\":true,\"read_only\":true,\"native_engine_started\":false}\n",checks);return 0;
}
