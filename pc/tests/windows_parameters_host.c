#include <windows.h>
#include <shellapi.h>
#include "windows_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x200000u
#define INPUT (BASE+0xc0000)
#define COUNT (BASE+0xd0000)
enum {CMD_A,CMD_W,PARSE,FREE,GET_ERROR,SET_ERROR,GET_HEAP,HEAP_SIZE,ALLOC,HEAP_FREE,REALLOC};
static const unsigned counts[]={0,0,2,1,0,1,0,3,3,3,4};
static unsigned char *arena;static struct fnv_windows_process *process;
static LPWSTR *(WINAPI *native_parse)(LPCWSTR,int *);
static const char command[]="\"C:\\FNV Test\\FalloutNV.exe\" -windowed \"test save\"";
static const char image_path[]="C:\\FNV Test\\FalloutNV.exe";
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static struct fnv_windows_thread_config thread_config(unsigned index){
    struct fnv_windows_thread_config t={0};uint32_t start=index?0x100000+(index-1)*0x20000:0x10000;
    t.thread_id=22+index;t.teb_address=BASE+start;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xc000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *args,uint32_t *value,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(stack,0x401500);for(unsigned i=0;i<counts[api];++i)put32(stack+4+i*4,args[i]);
    enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*32,fault);
    if(result==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+counts[api]*4);*value=s.gpr[0];}return result;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    uint32_t args[]={a,b,c,d},value;struct fnv_cpu_fault fault;
    enum fnv_cpu_result result=execute(api,args,&value,&fault);
    if(result!=FNV_CPU_OK)fprintf(stderr,"parameters API %u: %s\n",api,fault.text);
    assert(result==FNV_CPU_OK);return value;
}
static void text(const WCHAR *source){size_t n=wcslen(source);assert(n<=32767);for(size_t i=0;i<=n;++i){arena[INPUT-BASE+i*2]=(unsigned char)source[i];arena[INPUT-BASE+i*2+1]=(unsigned char)(source[i]>>8);}}
static void equal_wide(uint32_t at,const WCHAR *expected){
    for(size_t i=0;;++i){assert(i<=32767 && at>=BASE && (uint64_t)at+i*2+2<=BASE+ARENA);
        unsigned c=arena[at-BASE+i*2]|((unsigned)arena[at-BASE+i*2+1]<<8);assert(c==expected[i]);if(!c)break;}
}
static void compare(const WCHAR *input){
    text(input);put32(COUNT,0xdeadbeef);call(SET_ERROR,0x4567,0,0,0);
    uint32_t result=call(PARSE,INPUT,COUNT,0,0);assert(result && call(GET_ERROR,0,0,0,0)==0x4567);
    uint32_t count=read32(COUNT),heap=call(GET_HEAP,0,0,0,0),bytes=call(HEAP_SIZE,heap,0,result,0);
    int native_count;LPWSTR *native=native_parse(input,&native_count);assert(native);
    if(!*input){assert(count==1);equal_wide(read32(result),L"C:\\FNV Test\\FalloutNV.exe");}
    else{assert(count==(uint32_t)native_count);for(uint32_t i=0;i<count;++i){uint32_t at=read32(result+i*4);assert(at>=result+count*4 && at<result+bytes);equal_wide(at,native[i]);}}
    assert(!read32(result+count*4));assert(!LocalFree(native));assert(!call(FREE,result,0,0,0));
}
static DWORD WINAPI worker(void *context){
    unsigned index=(unsigned)(uintptr_t)context;struct fnv_windows_thread_config t=thread_config(index);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    for(unsigned i=0;i<250;++i){uint32_t result=call(PARSE,INPUT,COUNT+index*16,0,0);assert(result && read32(COUNT+index*16)==3);assert(!call(FREE,result,0,0,0));}
    assert(call(CMD_A,0,0,0,0)==BASE+0x20000);assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);HMODULE shell=LoadLibraryW(L"shell32.dll");assert(shell);native_parse=(void *)GetProcAddress(shell,"CommandLineToArgvW");assert(native_parse);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;
    assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.heap_address=BASE+0x50000;c.heap_bytes=0x60000;c.heap_capacity=1024;
    char mutable_command[sizeof(command)],mutable_path[sizeof(image_path)];memcpy(mutable_command,command,sizeof(command));memcpy(mutable_path,image_path,sizeof(image_path));
    c.parameters_address=BASE+0x20000;c.parameters_bytes=0x20000;c.command_line=mutable_command;c.image_path=mutable_path;
    struct fnv_windows_config invalid=c;invalid.parameters_address=c.heap_address;assert(fnv_windows_create(&invalid,&process)==FNV_CPU_INVALID && !process);
    invalid=c;invalid.parameters_address=c.peb_address;assert(fnv_windows_create(&invalid,&process)==FNV_CPU_INVALID && !process);
    invalid=c;invalid.parameters_bytes=1;assert(fnv_windows_create(&invalid,&process)==FNV_CPU_INVALID && !process);
    invalid=c;invalid.command_line="\xff";assert(fnv_windows_create(&invalid,&process)==FNV_CPU_INVALID && !process);
    if(argc==3 && !strcmp(argv[2],"--strict-unavailable")){c.parameters_address=c.parameters_bytes=0;c.command_line=c.image_path=NULL;}
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);mutable_command[0]='X';mutable_path[0]='X';struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==11);
    struct fnv_windows_thread_config t=thread_config(0),overlap=t;overlap.teb_address=c.parameters_address;
    if(argc==2)assert(fnv_windows_thread_attach(process,&overlap)==FNV_CPU_INVALID);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={!strcmp(argv[2],"--strict-wrap")?UINT32_MAX:INPUT,COUNT},value;struct fnv_cpu_fault fault;
        unsigned api=PARSE;if(!strcmp(argv[2],"--strict-input"))args[0]=0;else if(!strcmp(argv[2],"--strict-count"))args[1]=0;else if(!strcmp(argv[2],"--strict-unavailable"))api=CMD_A;
        assert(execute(api,args,&value,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);VirtualFree(arena,0,MEM_RELEASE);FreeLibrary(shell);puts("strict PASS");return 0;}
    uint32_t a=call(CMD_A,0,0,0,0),w=call(CMD_W,0,0,0,0);assert(a==c.parameters_address && a!=w);
    assert(!strcmp((char *)arena+a-BASE,command));for(size_t i=0;i<=strlen(command);++i)assert(arena[w-BASE+i*2]==(unsigned char)command[i] && !arena[w-BASE+i*2+1]);
    assert(call(CMD_A,0,0,0,0)==a && call(CMD_W,0,0,0,0)==w);assert(call(FREE,a,0,0,0)==a && call(GET_ERROR,0,0,0,0)==6);
    const WCHAR *cases[]={L"",L" ",L"\t x y",L"\"C:\\x y\\a.exe\" a b",L"p \"a\"\"b c\"",L"p \"\"\" x",L"p \"\"\"\" x",L"abc\"def ghi\" z",L"p \x263a \xd83d\xde00",L"\"\"a b"};
    unsigned sequences=0;for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i){compare(cases[i]);sequences++;}
    uint32_t random=0x6219abcd;WCHAR generated[160];const WCHAR alphabet[]=L"aZ \\\"\t\x263a";
    for(unsigned test=0;test<6000;++test){random=random*1664525+1013904223;unsigned n=random%159;for(unsigned i=0;i<n;++i){random=random*1664525+1013904223;generated[i]=alphabet[random%(sizeof(alphabet)/sizeof(*alphabet)-1)];}generated[n]=0;compare(generated);sequences++;}
    for(unsigned slashes=0;slashes<20;++slashes)for(unsigned quotes=1;quotes<14;++quotes){
        unsigned at=0;generated[at++]='p';generated[at++]=' ';generated[at++]='"';generated[at++]='a';
        for(unsigned j=0;j<slashes;++j)generated[at++]='\\';for(unsigned j=0;j<quotes;++j)generated[at++]='"';
        generated[at++]='b';generated[at++]=' ';generated[at++]='c';generated[at]=0;compare(generated);sequences++;
    }
    for(unsigned ch=1;ch<32;++ch){generated[0]='p';generated[1]=' ';generated[2]='a';generated[3]=(WCHAR)ch;generated[4]='b';generated[5]=0;compare(generated);sequences++;}
    WCHAR *maximum=malloc(32768*sizeof(*maximum));assert(maximum);for(unsigned i=0;i<32767;++i)maximum[i]='a';maximum[32767]=0;compare(maximum);free(maximum);sequences++;
    uint32_t heap=call(GET_HEAP,0,0,0,0);uint32_t full=call(ALLOC,heap,0,c.heap_bytes-8,0);assert(full);text(L"p a");put32(COUNT,0x12345678);
    assert(!call(PARSE,INPUT,COUNT,0,0) && call(GET_ERROR,0,0,0,0)==8 && read32(COUNT)==0x12345678);
    assert(call(FREE,full,0,0,0)==full);assert(call(HEAP_FREE,heap,0,full,0));
    uint32_t local=call(PARSE,INPUT,COUNT,0,0);assert(local);
    uint32_t blocker=call(ALLOC,heap,0,64,0);assert(blocker);uint32_t moved=call(REALLOC,heap,0,local,4096);assert(moved && moved!=local);
    assert(!call(FREE,moved,0,0,0) && call(HEAP_FREE,heap,0,blocker,0));
    local=call(PARSE,INPUT,COUNT,0,0);assert(local);assert(call(FREE,local+4,0,0,0)==local+4);assert(!call(FREE,local,0,0,0));assert(call(FREE,local,0,0,0)==local);
    assert(!call(FREE,0,0,0,0));text(L"p a b");HANDLE threads[4];for(unsigned i=0;i<4;++i){threads[i]=CreateThread(NULL,0,worker,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
    assert(WaitForMultipleObjects(4,threads,TRUE,30000)==0);for(unsigned i=0;i<4;++i){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}
    struct fnv_windows_heap_stats stats;assert(fnv_windows_heap_stats(process,&stats)==FNV_CPU_OK && !stats.live_blocks);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);assert(VirtualFree(arena,0,MEM_RELEASE));FreeLibrary(shell);
    printf("{\"status\":\"PASS\",\"windows_parser_sequences\":%u,\"concurrent_cycles\":1000,\"maximum_utf16_units\":32767,\"allocation_failure_atomic\":true,\"local_free_ownership\":true,\"native_engine_started\":false}\n",sequences);return 0;
}
