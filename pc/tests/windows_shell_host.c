#include <windows.h>
#include <shlobj.h>
#include "windows_thread.h"
#include "windows_shell.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define OUTPUT (BASE+0x80000u)
_Static_assert(CSIDL_PERSONAL==5 && CSIDL_LOCAL_APPDATA==0x1c,"Windows folder identifiers");
static unsigned char *arena;
int fnv_test_file_sync_failure,fnv_test_file_close_failure;
static _Thread_local uint32_t test_stack=BASE+0x1BF00;
static HANDLE io_entered,io_release,close_waiting;
static volatile LONG gate_enabled;
void fnv_test_file_io_gate(uint32_t operation){
    if(operation==2 && InterlockedCompareExchange(&gate_enabled,0,0)){
        assert(SetEvent(io_entered));assert(WaitForSingleObject(io_release,5000)==WAIT_OBJECT_0);
    }
}
void fnv_test_file_close_wait(void){if(close_waiting)assert(SetEvent(close_waiting));}
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t read32(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)arena[at-BASE+i]<<(8*i);return v;}
static uint32_t execute(unsigned api,const uint32_t *args,unsigned count,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state before,after;assert(fnv_cpu_get_state(&before)==FNV_CPU_OK);before.gpr[4]=test_stack;
    assert(fnv_cpu_set_state(&before)==FNV_CPU_OK);write32(before.gpr[4],0x401500);
    for(unsigned i=0;i<count;i++)write32(before.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[4]==before.gpr[4]+4+count*4);return after.gpr[0];}
    return UINT32_MAX;
}
struct close_race {struct fnv_windows_process *process;uint32_t handle,index;};
static DWORD WINAPI close_race_thread(void *context){
    struct close_race *r=context;struct fnv_windows_thread_config t={0};
    t.thread_id=100+r->index;t.teb_address=BASE+0x20000+r->index*0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);test_stack=t.stack_high-256;
    assert(fnv_windows_thread_attach(r->process,&t)==FNV_CPU_OK);struct fnv_cpu_fault fault;
    uint32_t args[5]={r->handle,BASE+0xa0000,3,BASE+0xb0000,0};
    if(r->index)assert(execute(8,args,1,&fault));else assert(execute(2,args,5,&fault));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
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
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==11);
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
    args[1]=CSIDL_PERSONAL;memset(arena+OUTPUT-BASE,0x5a,262);
    assert(execute(0,args,5,&fault)==0 && !strcmp((char *)(arena+OUTPUT-BASE),"C:\\Users\\FNV\\Documents"));
    assert(arena[OUTPUT-BASE+260]==0x5a && arena[OUTPUT-BASE+261]==0x5a && read32(t.teb_address+0x34)==0x5678);
    check_directory(root,"UserData/FNV/Documents");
    static const char my_games[]="C:\\Users\\FNV\\Documents\\My Games";
    memcpy(arena+0x96000,my_games,sizeof(my_games));uint32_t document_dir[2]={BASE+0x96000,0};
    assert(execute(6,document_dir,2,&fault));check_directory(root,"UserData/FNV/Documents/My Games");
    assert(!execute(6,document_dir,2,&fault) && read32(t.teb_address+0x34)==183);
    args[1]=CSIDL_LOCAL_APPDATA;assert(execute(0,args,5,&fault)==0 && !strcmp((char *)(arena+OUTPUT-BASE),expected));
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
    /* Exercise both update-stream directions without an intervening guest seek. */
    write_open[1]=GENERIC_READ|GENERIC_WRITE;write_open[4]=CREATE_ALWAYS;
    handle=execute(1,write_open,7,&fault);assert(handle!=UINT32_MAX);
    unsigned char sequence[20000];for(size_t i=0;i<sizeof(sequence);i++)sequence[i]=(unsigned char)(i*17);
    memcpy(arena+0xa0000,sequence,sizeof(sequence));write_args[1]=BASE+0xa0000;write_args[0]=handle;write_args[2]=sizeof(sequence);
    assert(execute(2,write_args,5,&fault));seek_args[1]=0;seek_args[0]=handle;assert(execute(4,seek_args,5,&fault));
    read_args[0]=handle;read_args[2]=3;assert(execute(7,read_args,5,&fault) && !memcmp(arena+0x91000,sequence,3));
    memcpy(arena+0xa0000,"XYZ",3);write_args[2]=3;assert(execute(2,write_args,5,&fault));
    assert(execute(7,read_args,5,&fault) && !memcmp(arena+0x91000,sequence+6,3));
    assert(execute(8,&handle,1,&fault));
    native=CreateFileA(fixture,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
    unsigned char verify_io[20000];assert(ReadFile(native,verify_io,sizeof(verify_io),&got,NULL) && got==sizeof(sequence) && CloseHandle(native));
    memcpy(sequence+3,"XYZ",3);assert(!memcmp(sequence,verify_io,sizeof(sequence)));
    /* Close errors propagate while the consumed guest handle stays invalid. */
    handle=execute(1,write_open,7,&fault);assert(handle!=UINT32_MAX);write_args[0]=handle;
    assert(execute(2,write_args,5,&fault));fnv_test_file_sync_failure=1;
    assert(!execute(3,&handle,1,&fault) && read32(t.teb_address+0x34)==112);fnv_test_file_sync_failure=0;
    assert(execute(8,&handle,1,&fault)); /* Close flushes, but does not request durable media sync. */
    assert(!execute(8,&handle,1,&fault) && read32(t.teb_address+0x34)==6);
    handle=execute(1,write_open,7,&fault);assert(handle!=UINT32_MAX);fnv_test_file_close_failure=1;
    assert(!execute(8,&handle,1,&fault) && read32(t.teb_address+0x34)==1117);fnv_test_file_close_failure=0;
    assert(!execute(8,&handle,1,&fault) && read32(t.teb_address+0x34)==6);
    /* A real in-flight write is pinned while CloseHandle waits for it. */
    handle=execute(1,write_open,7,&fault);assert(handle!=UINT32_MAX);
    io_entered=CreateEventA(NULL,TRUE,FALSE,NULL);io_release=CreateEventA(NULL,TRUE,FALSE,NULL);close_waiting=CreateEventA(NULL,TRUE,FALSE,NULL);
    assert(io_entered && io_release && close_waiting);InterlockedExchange(&gate_enabled,1);
    struct close_race race[2]={{process,handle,0},{process,handle,1}};
    HANDLE writing=CreateThread(NULL,0,close_race_thread,race,0,NULL);assert(writing);
    assert(WaitForSingleObject(io_entered,5000)==WAIT_OBJECT_0);
    HANDLE closing=CreateThread(NULL,0,close_race_thread,race+1,0,NULL);assert(closing);
    assert(WaitForSingleObject(close_waiting,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(closing,0)==WAIT_TIMEOUT && SetEvent(io_release));
    assert(WaitForSingleObject(writing,5000)==WAIT_OBJECT_0 && WaitForSingleObject(closing,5000)==WAIT_OBJECT_0);
    InterlockedExchange(&gate_enabled,0);assert(CloseHandle(writing) && CloseHandle(closing));
    assert(CloseHandle(io_entered) && CloseHandle(io_release) && CloseHandle(close_waiting));close_waiting=NULL;
    assert(!execute(8,&handle,1,&fault) && read32(t.teb_address+0x34)==6);
    /* Delete real profile files, but preserve game assets and open files. */
    handle=execute(1,open_args,7,&fault);assert(handle!=UINT32_MAX);
    uint32_t delete_args[1]={BASE+0x90000};
    assert(!execute(9,delete_args,1,&fault) && read32(t.teb_address+0x34)==32);
    assert(execute(8,&handle,1,&fault));
    open_args[2]|=FILE_SHARE_DELETE;handle=execute(1,open_args,7,&fault);assert(handle!=UINT32_MAX);
    assert(!execute(9,delete_args,1,&fault) && read32(t.teb_address+0x34)==50);
    assert(GetFileAttributesA(fixture)!=INVALID_FILE_ATTRIBUTES && execute(8,&handle,1,&fault));
    open_args[2]&=~FILE_SHARE_DELETE;
    write32(t.teb_address+0x34,0x1234);assert(execute(9,delete_args,1,&fault) && read32(t.teb_address+0x34)==0x1234);
    assert(GetFileAttributesA(fixture)==INVALID_FILE_ATTRIBUTES && GetLastError()==2);
    assert(!execute(9,delete_args,1,&fault) && read32(t.teb_address+0x34)==2);
    assert(!execute(9,document_dir,1,&fault) && read32(t.teb_address+0x34)==5);
    char protected_path[2048];int protected_size=snprintf(protected_path,sizeof(protected_path),"%s/protected.txt",root);
    assert(protected_size>0 && (size_t)protected_size<sizeof(protected_path));
    native=CreateFileA(protected_path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE && CloseHandle(native));
    memcpy(arena+0x97000,"protected.txt",sizeof("protected.txt"));uint32_t root_delete[1]={BASE+0x97000};
    assert(!execute(9,root_delete,1,&fault) && read32(t.teb_address+0x34)==5);
    assert(GetFileAttributesA(protected_path)!=INVALID_FILE_ATTRIBUTES && DeleteFileA(protected_path));
    memcpy(arena+0x97000,"Warnings.txt",sizeof("Warnings.txt"));
    assert(!execute(9,root_delete,1,&fault) && read32(t.teb_address+0x34)==2);
    memcpy(arena+0x97000,"absent\\missing.txt",sizeof("absent\\missing.txt"));
    assert(!execute(9,root_delete,1,&fault) && read32(t.teb_address+0x34)==3);
    /* Exclusive CopyFileA: compare bytes and collision behavior with Windows. */
    char copy_source[2048],copy_dest[2048],oracle_dest[2048];
    assert(snprintf(copy_source,sizeof(copy_source),"%s/copy-source.ini",root)>0);
    assert(snprintf(copy_dest,sizeof(copy_dest),"%s/UserData/FNV/Documents/copy.ini",root)>0);
    assert(snprintf(oracle_dest,sizeof(oracle_dest),"%s/oracle-copy.ini",root)>0);
    unsigned char copy_bytes[131089];for(size_t i=0;i<sizeof(copy_bytes);++i)copy_bytes[i]=(unsigned char)(i*37);
    native=CreateFileA(copy_source,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE);
    assert(WriteFile(native,copy_bytes,sizeof(copy_bytes),&written,NULL) && written==sizeof(copy_bytes) && CloseHandle(native));
    memcpy(arena+0x98000,"copy-source.ini",sizeof("copy-source.ini"));
    memcpy(arena+0x99000,"C:\\Users\\FNV\\Documents\\copy.ini",sizeof("C:\\Users\\FNV\\Documents\\copy.ini"));
    uint32_t copy_args[3]={BASE+0x98000,BASE+0x99000,1};
    write32(t.teb_address+0x34,0x1234);assert(execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==0x1234);
    assert(CopyFileA(copy_source,oracle_dest,TRUE));
    FILE *actual=fopen(copy_dest,"rb"),*oracle=fopen(oracle_dest,"rb");assert(actual && oracle);
    for(size_t i=0;i<sizeof(copy_bytes);++i){assert(fgetc(actual)==copy_bytes[i]);assert(fgetc(oracle)==copy_bytes[i]);}
    assert(fgetc(actual)==EOF && fgetc(oracle)==EOF);assert(!fclose(actual) && !fclose(oracle));
    assert(!CopyFileA(copy_source,oracle_dest,TRUE) && GetLastError()==80);
    assert(!execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==80);
    /* Unsupported overwrite must preserve the already copied content. */
    copy_args[2]=0;assert(!execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==50);copy_args[2]=1;
    assert(DeleteFileA(copy_dest));
    uint32_t source_open[7]={BASE+0x98000,GENERIC_READ,0,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0};
    handle=execute(1,source_open,7,&fault);assert(handle!=UINT32_MAX);
    assert(!execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==32);
    assert(execute(8,&handle,1,&fault) && GetFileAttributesA(copy_dest)==INVALID_FILE_ATTRIBUTES);
    /* Empty files, absent source, and denied asset destination. */
    native=CreateFileA(copy_source,GENERIC_WRITE,0,NULL,TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);assert(native!=INVALID_HANDLE_VALUE && CloseHandle(native));
    assert(execute(10,copy_args,3,&fault));actual=fopen(copy_dest,"rb");assert(actual && fgetc(actual)==EOF && !fclose(actual));
    assert(DeleteFileA(copy_source) && DeleteFileA(copy_dest) && DeleteFileA(oracle_dest));
    assert(!execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==2);
    copy_args[1]=BASE+0x98000;assert(!execute(10,copy_args,3,&fault) && read32(t.teb_address+0x34)==50);
    static const char relative_path[]="Data\\user-write-denied.bin";memcpy(arena+0x90000,relative_path,sizeof(relative_path));
    write_open[0]=BASE+0x90000;uint32_t denied=execute(1,write_open,7,&fault);assert(denied==UINT32_MAX && read32(t.teb_address+0x34)==50);
    static const char escape_path[]="C:\\Users\\FNV\\..\\escape.bin";memcpy(arena+BASE+0x90000-BASE,escape_path,sizeof(escape_path));
    open_args[0]=BASE+0x90000;assert(execute(1,open_args,7,&fault)==UINT32_MAX && read32(t.teb_address+0x34)==5);
    struct fnv_windows_file_stats file_stats;assert(fnv_windows_file_stats(process,&file_stats)==FNV_CPU_OK);
    assert(file_stats.writes==5 && file_stats.write_bytes==20016 && file_stats.syncs==4 && file_stats.sync_errors==1);
    assert(file_stats.closes && file_stats.close_errors==1);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"api\":\"SHELL32!SHGetFolderPathA\",\"csidl\":28,\"mapped_root\":\"UserData/FNV/AppData/Local\",\"documents_profile\":true,\"alternating_read_write\":true,\"close_flushes_without_durable_sync\":true,\"explicit_flush_error_propagates\":true,\"close_waits_for_pinned_write\":true,\"exclusive_profile_copy\":true,\"profile_delete\":true,\"readonly_game_delete_denied\":true,\"profile_directory_create\":true,\"profile_file_read\":true,\"profile_create_truncate_write\":true,\"set_end_of_file_truncate\":true,\"flush_write_handle\":true,\"file_io_stats\":true,\"readonly_access_rejected\":true,\"relative_game_mutation_rejected\":true,\"traversal_rejected\":true,\"last_error_preserved\":true,\"engine_started\":false}\n");return 0;
}
