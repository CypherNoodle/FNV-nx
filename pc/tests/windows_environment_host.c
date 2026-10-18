#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
enum {GET_A,GET_W,FREE_A,FREE_W,GET_ERROR,SET_ERROR,HEAP,ALLOC,FREE,LOCAL_FREE};
static unsigned char *arena;
static const unsigned arguments[]={0,0,1,1,0,1,0,3,3,1};
static void write32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c){
    struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);write32(state.gpr[4],0x401500);
    write32(state.gpr[4]+4,a);write32(state.gpr[4]+8,b);write32(state.gpr[4]+12,c);
    struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+api*32,&fault)==FNV_CPU_OK);
    assert(fnv_cpu_get_state(&state)==FNV_CPU_OK && state.gpr[4]==BASE+0x1bf04+arguments[api]*4);return state.gpr[0];
}
static void content(uint32_t at,const char *expected,size_t bytes,int wide){
    assert(at>=BASE && (uint64_t)at+bytes*(wide?2:1)<=BASE+ARENA);
    for(size_t i=0;i<bytes;i++){assert(arena[at-BASE+i*(wide?2:1)]==(unsigned char)expected[i]);if(wide)assert(!arena[at-BASE+i*2+1]);}
}
static int contains(const wchar_t *block,const wchar_t *entry){
    for(const wchar_t *p=block;*p;p+=wcslen(p)+1)if(!wcscmp(p,entry))return 1;return 0;
}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    c.heap_address=BASE+0x80000;c.heap_bytes=0x10000;c.heap_capacity=16;
    struct fnv_windows_process *process;
    const char invalid[][8]={"A\0\0","=B\0\0","A=B\0\0x","A=\xff\0\0"};
    const uint32_t lengths[]={3,4,6,5};
    for(unsigned i=0;i<4;i++){c.environment=invalid[i];c.environment_bytes=lengths[i];assert(fnv_windows_create(&c,&process)==FNV_CPU_INVALID && !process);}
    c.environment="\0";c.environment_bytes=1;assert(fnv_windows_create(&c,&process)==FNV_CPU_INVALID && !process);
    char snapshot[]="FNX_ENV_A=path with spaces\0FNX_ENV_B=\0FNX_ENV_C=a=b\0";
    const char expected[]="FNX_ENV_A=path with spaces\0FNX_ENV_B=\0FNX_ENV_C=a=b\0";
    c.environment=snapshot;c.environment_bytes=sizeof(snapshot);
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);memset(snapshot,'!',sizeof(snapshot));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==10);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    unsigned cycles=0;
    for(unsigned i=0;i<2000;i++){
        call(SET_ERROR,0x3456,0,0);uint32_t a=call(GET_A,0,0,0),w=call(GET_W,0,0,0);assert(a && w && a!=w && call(GET_ERROR,0,0,0)==0x3456);
        content(a,expected,sizeof(expected),0);content(w,expected,sizeof(expected),1);
        assert(!call(FREE_A,w,0,0) && call(GET_ERROR,0,0,0)==87);
        assert(call(LOCAL_FREE,w,0,0)==w && call(GET_ERROR,0,0,0)==6);
        call(SET_ERROR,0x3456,0,0);assert(call(FREE_W,w,0,0) && call(FREE_A,a,0,0) && call(GET_ERROR,0,0,0)==0x3456);
        assert(!call(FREE_A,a,0,0) && !call(FREE_W,0,0,0));cycles++;
    }
    uint32_t heap=call(HEAP,0,0,0),ordinary=call(ALLOC,heap,0,64);assert(ordinary && !call(FREE_A,ordinary,0,0));assert(call(FREE,heap,0,ordinary));
    uint32_t blocks[16];for(unsigned i=0;i<16;i++)assert((blocks[i]=call(GET_W,0,0,0)));
    assert(!call(GET_W,0,0,0) && call(GET_ERROR,0,0,0)==8);
    for(unsigned i=0;i<16;i++){content(blocks[i],expected,sizeof(expected),1);assert(call(FREE_W,blocks[i],0,0));}
    struct fnv_windows_heap_stats stats;assert(fnv_windows_heap_stats(process,&stats)==FNV_CPU_OK && !stats.live_blocks);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);c.environment=NULL;c.environment_bytes=0;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t empty=call(GET_W,0,0,0);assert(empty);content(empty,"\0",2,1);assert(call(FREE_W,empty,0,0));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(SetEnvironmentVariableW(L"FNX_ENV_A",L"path with spaces") && SetEnvironmentVariableW(L"FNX_ENV_C",L"a=b"));
    SetLastError(0x3456);LPWCH native=GetEnvironmentStringsW();assert(native && GetLastError()==0x3456);
    assert(contains(native,L"FNX_ENV_A=path with spaces") && contains(native,L"FNX_ENV_C=a=b"));
    assert(FreeEnvironmentStringsW(native) && GetLastError()==0x3456);
    assert(VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"copy_free_cycles\":%u,\"snapshot_immutable\":true,\"empty_environment\":true,\"allocation_exhaustion\":true,\"matching_owner_checked\":true,\"windows_selected_ASCII_values\":true,\"native_engine_started\":false}\n",cycles);return 0;
}
