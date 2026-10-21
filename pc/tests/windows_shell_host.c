#include <windows.h>
#include "windows_thread.h"
#include "windows_shell.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define OUTPUT (BASE+0x80000u)
static unsigned char *arena;
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t read32(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)arena[at-BASE+i]<<(8*i);return v;}
static uint32_t execute(unsigned api,const uint32_t *args,unsigned count,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state before,after;assert(fnv_cpu_get_state(&before)==FNV_CPU_OK);before.gpr[4]=BASE+0x1BF00;
    assert(fnv_cpu_set_state(&before)==FNV_CPU_OK);write32(before.gpr[4],0x401500);
    for(unsigned i=0;i<count;i++)write32(before.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[4]==before.gpr[4]+4+count*4);return after.gpr[0];}
    return UINT32_MAX;
}
static void check_directory(const char *root,const char *suffix){char path[2048];
    int n=snprintf(path,sizeof(path),"%s/%s",root,suffix);assert(n>0 && (size_t)n<sizeof(path));DWORD attr=GetFileAttributesA(path);
    assert(attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY));}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    char root[1024];assert(GetFullPathNameA(argv[1],sizeof(root),root,NULL));char *end=strrchr(root,'\\');assert(end);*end=0;
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    c.file_root=root;c.file_handle_capacity=4;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_shell_exports(&n);assert(n==1);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==9);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t args[5]={0,0x1c,0,0,OUTPUT};
    if(argc==3){const char *mode=argv[2];if(!strcmp(mode,"--strict-csidl"))args[1]=0x1d;
        else if(!strcmp(mode,"--strict-flags"))args[3]=1;else if(!strcmp(mode,"--strict-hwnd"))args[0]=1;
        else if(!strcmp(mode,"--strict-token"))args[2]=1;else args[4]=BASE+BYTES;
        struct fnv_cpu_fault fault;assert(execute(0,args,5,&fault)==UINT32_MAX && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    memset(arena+OUTPUT-BASE,0x5a,260);arena[t.teb_address-BASE+0x34]=0x78;arena[t.teb_address-BASE+0x35]=0x56;
    struct fnv_cpu_fault fault;assert(execute(0,args,5,&fault)==0);static const char expected[]="C:\\Users\\FNV\\AppData\\Local";
    assert(!strcmp((char *)(arena+OUTPUT-BASE),expected));assert(arena[OUTPUT-BASE+sizeof(expected)]==0);
    assert(arena[t.teb_address-BASE+0x34]==0x78 && arena[t.teb_address-BASE+0x35]==0x56);
    check_directory(root,"UserData");check_directory(root,"UserData/FNV");check_directory(root,"UserData/FNV/AppData");check_directory(root,"UserData/FNV/AppData/Local");
    char fixture[2048];int path_bytes=snprintf(fixture,sizeof(fixture),"%s/UserData/FNV/AppData/Local/fixture.bin",root);assert(path_bytes>0 && (size_t)path_bytes<sizeof(fixture));
    HANDLE native=CreateFileA(fixture,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
    static const unsigned char payload[]={0x46,0x4e,0x56,0x2d,0x55,0x44,0x31};DWORD written=0;assert(WriteFile(native,payload,sizeof(payload),&written,NULL) && written==sizeof(payload) && CloseHandle(native));
    static const char guest_path[]="C:\\Users\\FNV\\AppData\\Local\\fixture.bin";memcpy(arena+BASE+0x90000-BASE,guest_path,sizeof(guest_path));
    uint32_t open_args[7]={BASE+0x90000,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0};uint32_t handle=execute(1,open_args,7,&fault);
    if(handle==UINT32_MAX)fprintf(stderr,"profile open failed, last error=%u path=%s fixture=%s\n",read32(t.teb_address+0x34),(char *)(arena+0x90000),fixture);
    assert(handle!=UINT32_MAX);
    uint32_t read_args[5]={handle,BASE+0x91000,sizeof(payload),BASE+0x92000,0};assert(execute(7,read_args,5,&fault) && !memcmp(arena+0x91000,payload,sizeof(payload)));
    uint32_t denied_write[5]={handle,BASE+0x93000,sizeof(payload),BASE+0x92000,0};
    assert(!execute(2,denied_write,5,&fault) && read32(t.teb_address+0x34)==5 && !read32(BASE+0x92000));
    assert(!execute(3,&handle,1,&fault) && read32(t.teb_address+0x34)==5);
    assert(!execute(5,&handle,1,&fault) && read32(t.teb_address+0x34)==5);
    assert(execute(8,&handle,1,&fault));
    static const char profile_dir[]="C:\\Users\\FNV\\AppData\\Local\\My Games";memcpy(arena+0x94000,profile_dir,sizeof(profile_dir));
    uint32_t directory_args[2]={BASE+0x94000,0};assert(execute(6,directory_args,2,&fault));check_directory(root,"UserData/FNV/AppData/Local/My Games");
    assert(!execute(6,directory_args,2,&fault) && read32(t.teb_address+0x34)==183);
    static const char denied_dir[]="Data\\user-write-denied";memcpy(arena+0x94000,denied_dir,sizeof(denied_dir));
    assert(!execute(6,directory_args,2,&fault) && read32(t.teb_address+0x34)==50);
    /* Writes are confined to the mapped profile, and CREATE_ALWAYS truncates. */
    memset(arena+0x93000,0x6b,sizeof(payload));
    uint32_t write_open[7]={BASE+0x90000,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0};
    write_open[4]=CREATE_NEW;assert(execute(1,write_open,7,&fault)==UINT32_MAX && read32(t.teb_address+0x34)==80);
    write_open[4]=CREATE_ALWAYS;
    handle=execute(1,write_open,7,&fault);assert(handle!=UINT32_MAX && read32(t.teb_address+0x34)==183);
    uint32_t write_args[5]={handle,BASE+0x93000,sizeof(payload),BASE+0x92000,0};
    read_args[0]=handle;assert(!execute(7,read_args,5,&fault) && read32(t.teb_address+0x34)==5);
    assert(execute(2,write_args,5,&fault) && read32(BASE+0x92000)==sizeof(payload));
    uint32_t seek_args[5]={handle,3,0,0,FILE_BEGIN};assert(execute(4,seek_args,5,&fault));
    assert(execute(5,&handle,1,&fault));assert(execute(3,&handle,1,&fault));assert(execute(8,&handle,1,&fault));
    native=CreateFileA(fixture,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
    unsigned char verify[sizeof(payload)];DWORD got=0;assert(ReadFile(native,verify,sizeof(verify),&got,NULL) && got==3);
    for(size_t i=0;i<got;++i){assert(verify[i]==0x6b);}
    CloseHandle(native);
    static const char relative_path[]="Data\\user-write-denied.bin";memcpy(arena+0x90000,relative_path,sizeof(relative_path));
    write_open[0]=BASE+0x90000;uint32_t denied=execute(1,write_open,7,&fault);assert(denied==UINT32_MAX && read32(t.teb_address+0x34)==50);
    static const char escape_path[]="C:\\Users\\FNV\\..\\escape.bin";memcpy(arena+BASE+0x90000-BASE,escape_path,sizeof(escape_path));
    open_args[0]=BASE+0x90000;assert(execute(1,open_args,7,&fault)==UINT32_MAX && read32(t.teb_address+0x34)==5);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"api\":\"SHELL32!SHGetFolderPathA\",\"csidl\":28,\"mapped_root\":\"UserData/FNV/AppData/Local\",\"profile_directory_create\":true,\"profile_file_read\":true,\"profile_create_truncate_write\":true,\"set_end_of_file_truncate\":true,\"flush_write_handle\":true,\"readonly_access_rejected\":true,\"relative_game_mutation_rejected\":true,\"traversal_rejected\":true,\"last_error_preserved\":true,\"engine_started\":false}\n");return 0;
}
