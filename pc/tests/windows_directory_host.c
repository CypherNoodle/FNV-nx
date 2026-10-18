#include <windows.h>
#include <winioctl.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define NAME_AT (BASE+0x9000)
#define OUTPUT (BASE+0xa000)
enum {ATTR_A,ATTR_W,FIRST_A,FIRST_W,NEXT_A,NEXT_W,FIND_CLOSE,FILE_TYPE,CLOSE,OPEN_A,GET_ERROR,SET_ERROR};
static const unsigned counts[]={1,1,2,2,2,2,1,1,1,7,0,1};
static unsigned char *arena;static struct fnv_windows_process *process;
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *a,uint32_t *v,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put32(stack,0x401500);for(unsigned i=0;i<counts[api];++i)put32(stack+4+i*4,a[i]);enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+counts[api]*4);*v=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b){uint32_t args[]={a,b},v=0;struct fnv_cpu_fault f;
    enum fnv_cpu_result r=execute(api,args,&v,&f);if(r!=FNV_CPU_OK)fprintf(stderr,"directory API %u: %s\n",api,f.text);assert(r==FNV_CPU_OK);return v;}
static uint32_t path(const char *s,int wide){size_t n=strlen(s);assert(n<512);for(size_t i=0;i<=n;++i){arena[NAME_AT-BASE+i*(wide?2:1)]=(unsigned char)s[i];if(wide)arena[NAME_AT-BASE+i*2+1]=0;}return NAME_AT;}
static void write_file(const char *path){HANDLE h=CreateFileA(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(h!=INVALID_HANDLE_VALUE);DWORD count;
    assert(WriteFile(h,"fixture data",12,&count,NULL) && count==12 && CloseHandle(h));}
struct item {char name[260],alternate[14];unsigned char metadata[44];};
static int compare_item(const void *a,const void *b){return strcmp(((const struct item *)a)->name,((const struct item *)b)->name);}
static unsigned guest_entries(const char *query,int wide,struct item *items,unsigned capacity){
    memset(arena+OUTPUT-BASE,0xa5,608);uint32_t h=call(wide?FIRST_W:FIRST_A,path(query,wide),OUTPUT);unsigned n=0;
    if(h==UINT32_MAX){assert(call(GET_ERROR,0,0)==2 || call(GET_ERROR,0,0)==3);return 0;}
    assert((h&0xf0000000u)==0xc0000000u);
    do {assert(n<capacity);struct item *e=items+n;memset(e,0,sizeof(*e));memcpy(e->metadata,arena+OUTPUT-BASE,44);
        for(unsigned i=0;i<260;++i){e->name[i]=(char)arena[OUTPUT-BASE+44+i*(wide?2:1)];if(wide)assert(!arena[OUTPUT-BASE+45+i*2]);if(!e->name[i])break;}
        for(unsigned i=0;i<14;++i){e->alternate[i]=(char)arena[OUTPUT-BASE+(wide?564:304)+i*(wide?2:1)];if(wide)assert(!arena[OUTPUT-BASE+565+i*2]);if(!e->alternate[i])break;}
        assert(strcmp(e->name,".") && strcmp(e->name,".."));n++;
        for(unsigned i=wide?592:320;i<608;++i)assert(arena[OUTPUT-BASE+i]==0xa5);
    }while(call(wide?NEXT_W:NEXT_A,h,OUTPUT));
    assert(call(GET_ERROR,0,0)==18);memset(arena+OUTPUT-BASE,0xa5,608);assert(!call(wide?NEXT_W:NEXT_A,h,OUTPUT) && call(GET_ERROR,0,0)==18);
    for(unsigned i=0;i<608;++i)assert(arena[OUTPUT-BASE+i]==0xa5);
    assert(!call(CLOSE,h,0) && call(GET_ERROR,0,0)==6);assert(call(FIND_CLOSE,h,0));
    assert(!call(FIND_CLOSE,h,0) && call(GET_ERROR,0,0)==6);
    qsort(items,n,sizeof(*items),compare_item);return n;
}
static unsigned native_entries(const char *root,const char *query,struct item *items,unsigned capacity){
    char full[2048];snprintf(full,sizeof(full),"%s/%s",root,query);WIN32_FIND_DATAA data;HANDLE h=FindFirstFileA(full,&data);unsigned n=0;
    if(h==INVALID_HANDLE_VALUE){assert(GetLastError()==2 || GetLastError()==3);return 0;}
    do {if(!strcmp(data.cFileName,".") || !strcmp(data.cFileName,".."))continue;assert(n<capacity);struct item *e=items+n++;memset(e,0,sizeof(*e));
        memcpy(e->metadata,&data,44);strcpy(e->name,data.cFileName);strcpy(e->alternate,data.cAlternateFileName);
    }while(FindNextFileA(h,&data));assert(GetLastError()==18 && FindClose(h));qsort(items,n,sizeof(*items),compare_item);return n;
}
static struct fnv_windows_thread_config thread_config(unsigned index){struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x10000;
    t.tls_expansion_address=t.teb_address+4096;t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;}
static struct item shared_expected[32];static volatile LONG seen[32];static unsigned expected_count;static uint32_t shared_search;
static void mark_item(uint32_t out){const char *name=(const char *)(arena+out-BASE+44);unsigned i=0;while(i<expected_count && strcmp(name,shared_expected[i].name))i++;
    assert(i<expected_count && InterlockedCompareExchange(&seen[i],1,0)==0);}
static DWORD WINAPI enumerate_worker(void *context){unsigned index=(unsigned)(uintptr_t)context;struct fnv_windows_thread_config t=thread_config(index);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t out=BASE+0x80000+index*4096;
    while(call(NEXT_A,shared_search,out))mark_item(out);assert(call(GET_ERROR,0,0)==18);assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;}
static void compare_sets(struct item *got,struct item *expected,unsigned a,unsigned b){
    assert(a==b);for(unsigned j=0;j<a;++j){assert(!strcmp(got[j].name,expected[j].name) && !strcmp(got[j].alternate,expected[j].alternate));assert(!memcmp(got[j].metadata,expected[j].metadata,44));}}
static int audit_game(const char *fixture,const char *root){
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(fixture,arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=4;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK);
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    struct item *got=calloc(1024,sizeof(*got)),*expected=calloc(1024,sizeof(*expected));assert(got && expected);
    const char *queries[]={"Data/*.bsa","Data/*.esm"};unsigned total=0;
    for(unsigned wide=0;wide<2;++wide)for(unsigned i=0;i<2;++i){unsigned a=guest_entries(queries[i],wide,got,1024),b=native_entries(root,queries[i],expected,1024);assert(a);compare_sets(got,expected,a,b);total+=a;}
    free(got);free(expected);assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"real_game_search_sets\":4,\"real_metadata_entries_compared\":%u,\"game_code_executed\":false}\n",total);return 0;
}
int main(int argc,char **argv){
    if(argc==4 && !strcmp(argv[2],"--audit-root"))return audit_game(argv[1],argv[3]);
    _Static_assert(sizeof(WIN32_FIND_DATAA)==320,"ANSI find layout");_Static_assert(sizeof(WIN32_FIND_DATAW)==592,"UTF16 find layout");
    assert(argc==2 || argc==3);char root[2048],data[2048];strcpy(root,argv[1]);char *end=strrchr(root,'\\'),*slash=strrchr(root,'/');if(slash && (!end || slash>end))end=slash;assert(end);*end=0;
    strcat(root,"/enum-root");if(argc==3)strcat(root,argv[2]);assert(CreateDirectoryA(root,NULL));snprintf(data,sizeof(data),"%s/Data",root);assert(CreateDirectoryA(data,NULL));
    const char *names[]={"FalloutNV.esm","Fallout - Textures.bsa","UPPER.BSA","small.txt","plain","thing.tar.gz","prefix.dat","mixed.BsA","read.only","dot.txt",".hidden"};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i){char full[2048];snprintf(full,sizeof(full),"%s/%s",data,names[i]);write_file(full);}
    char sub[2048];snprintf(sub,sizeof(sub),"%s/Subdir",data);assert(CreateDirectoryA(sub,NULL));
    if(argc==2){char full[2048];snprintf(full,sizeof(full),"%s/large.bsa",data);HANDLE h=CreateFileA(full,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(h!=INVALID_HANDLE_VALUE);
        DWORD returned,written;assert(DeviceIoControl(h,FSCTL_SET_SPARSE,NULL,0,NULL,0,&returned,NULL));LARGE_INTEGER at;at.QuadPart=INT64_C(0x1fffffffe);
        assert(SetFilePointerEx(h,at,NULL,FILE_BEGIN) && WriteFile(h,"x",1,&written,NULL) && written==1 && CloseHandle(h));}
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=4;
    if(argc==3 && !strcmp(argv[2],"--strict-disabled")){c.file_root=NULL;c.file_handle_capacity=0;}
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==12);
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){
        if(!strcmp(argv[2],"--strict-unicode")){
            WCHAR full[2048];assert(MultiByteToWideChar(CP_ACP,0,data,-1,full,2000));size_t len=wcslen(full);full[len++]=L'/';full[len++]=0xe9;full[len]=0;
            HANDLE h=CreateFileW(full,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);assert(h!=INVALID_HANDLE_VALUE && CloseHandle(h));
            struct item items[32];assert(guest_entries("Data/*",0,items,32)==12);
            assert(guest_entries("Data/*",1,items,32)==12);
            struct fnv_windows_file_stats skipped;assert(fnv_windows_file_stats(process,&skipped)==FNV_CPU_OK && skipped.skipped_non_ascii==2 && !skipped.live_searches);
        }else {uint32_t a[]={path("Data/*",0),BASE+ARENA-1},value=0;struct fnv_cpu_fault f;
            assert(execute(FIRST_A,a,&value,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED);}
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    const char *queries[]={"Data/*","Data/*.*","Data/*.bsa","Data/*.BSA","Data/*.esm","Data/plain","Data/plain.*","Data/prefix*","Data/*.txt","Data/*.","Data/no-match*","missing/*.bsa"};unsigned matches=0;
    for(unsigned wide=0;wide<2;++wide)for(unsigned i=0;i<sizeof(queries)/sizeof(*queries);++i){struct item got[32],expected[32];unsigned a=guest_entries(queries[i],wide,got,32),b=native_entries(root,queries[i],expected,32);
        if(a!=b)fprintf(stderr,"pattern %s got%u expected%u\n",queries[i],a,b);assert(a==b);
        compare_sets(got,expected,a,b);matches+=a;
    }
    const char *paths[]={".","Data","Data/Subdir","Data/plain","Data/missing","missing/plain"};
    for(unsigned wide=0;wide<2;++wide)for(unsigned i=0;i<6;++i){char full[2048];snprintf(full,sizeof(full),"%s/%s",root,paths[i]);call(SET_ERROR,0x9876,0);
        uint32_t got=call(wide?ATTR_W:ATTR_A,path(paths[i],wide),0);SetLastError(0x9876);DWORD expected=GetFileAttributesA(full),error=GetLastError();assert(got==expected);if(got==UINT32_MAX)assert(call(GET_ERROR,0,0)==error);}
    uint32_t args[]={path("Data/plain",0),GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0},h=0;struct fnv_cpu_fault fault;assert(execute(OPEN_A,args,&h,&fault)==FNV_CPU_OK && h!=UINT32_MAX);
    assert(call(FILE_TYPE,h,0)==FILE_TYPE_DISK && !call(FIND_CLOSE,h,0) && call(CLOSE,h,0));
    assert(!call(FILE_TYPE,h,0) && call(GET_ERROR,0,0)==6);
    assert(call(FIRST_A,path("Data/?.bsa",0),OUTPUT)==UINT32_MAX && call(GET_ERROR,0,0)==50);
    uint32_t searches[4];for(unsigned i=0;i<4;++i){searches[i]=call(FIRST_A,path("Data/*",0),OUTPUT);assert(searches[i]!=UINT32_MAX);}
    assert(call(FIRST_A,path("Data/*",0),OUTPUT)==UINT32_MAX && call(GET_ERROR,0,0)==4);
    for(unsigned i=0;i<4;++i)assert(call(FIND_CLOSE,searches[i],0));
    expected_count=native_entries(root,"Data/*",shared_expected,32);shared_search=call(FIRST_A,path("Data/*",0),OUTPUT);assert(shared_search!=UINT32_MAX);mark_item(OUTPUT);
    assert(!call(CLOSE,shared_search^0x10000000u,0) && call(GET_ERROR,0,0)==6);
    HANDLE threads[4];for(unsigned i=0;i<4;++i){threads[i]=CreateThread(NULL,0,enumerate_worker,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,10000)==0);for(unsigned i=0;i<4;++i){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    for(unsigned i=0;i<expected_count;++i)assert(seen[i]==1);assert(call(FIND_CLOSE,shared_search,0));
    struct fnv_windows_file_stats stats;assert(fnv_windows_file_stats(process,&stats)==FNV_CPU_OK && !stats.live_handles && !stats.live_searches && stats.searches && stats.attribute_queries==12);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_search_sets\":24,\"metadata_entries_compared\":%u,\"shared_cursor_entries\":%u,\"attribute_comparisons\":12,\"ansi_bytes\":320,\"utf16_bytes\":592,\"native_engine_started\":false}\n",matches,expected_count);return 0;
}
