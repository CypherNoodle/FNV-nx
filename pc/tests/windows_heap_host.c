#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x200000u
#define HEAP_BASE (BASE+0x40000u)
#define HEAP_BYTES 0x100000u
enum {PROCESS_HEAP,ALLOC,REALLOC,FREE,HEAP_SIZE,VALIDATE,GET_ERROR,SET_ERROR};
static unsigned char *arena;
static struct fnv_windows_process *process;
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void write32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;++i)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    const unsigned argc[]={0,3,4,3,3,3,0,1};struct fnv_cpu_state s,got;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1BF00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    uint32_t values[]={0x401500,a,b,c,d};for(unsigned i=0;i<5;++i)write32(s.gpr[4]+i*4,values[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"heap API %u fault: %s\n",api,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[4]==s.gpr[4]+4+argc[api]*4);return got.gpr[0];
}
static struct fnv_windows_thread_config thread_config(unsigned index){
    struct fnv_windows_thread_config t={0};t.thread_id=22+index;t.teb_address=BASE+0x10000+index*0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0xC000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);return t;
}
static DWORD WINAPI worker(void *unused){
    (void)unused;struct fnv_windows_thread_config t=thread_config(1);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    /* Own guest stack: call() uses a separate stack location per host thread below. */
    for(unsigned n=0;n<1000;++n){
        /* Execute through per-thread stack by direct runtime setup. */
        struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
        uint32_t args[]={0x401500,HEAP_BASE,8,64};uint32_t at=t.stack_high-128;
        for(unsigned j=0;j<4;++j)write32(at+j*4,args[j]);s.gpr[4]=at;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
        assert(fnv_cpu_execute(0x401520,NULL)==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK);uint32_t block=s.gpr[0];assert(block);
        memset(arena+block-BASE,0x79,64);args[2]=0;args[3]=block;for(unsigned j=0;j<4;++j)write32(at+j*4,args[j]);s.gpr[4]=at;
        assert(fnv_cpu_set_state(&s)==FNV_CPU_OK && fnv_cpu_execute(0x401560,NULL)==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[0]==1);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=ARENA;
    cfg.cpu.code_low=BASE+0x1000;cfg.cpu.code_high=BASE+image.size;cfg.image_bytes=image.size;cfg.process_id=11;cfg.peb_address=BASE+0x8000;
    cfg.heap_address=HEAP_BASE;cfg.heap_bytes=HEAP_BYTES;cfg.heap_capacity=128;
    struct fnv_windows_config bad=cfg;bad.heap_address=cfg.peb_address;assert(fnv_windows_create(&bad,&process)==FNV_CPU_INVALID && !process);
    bad=cfg;bad.heap_address++;assert(fnv_windows_create(&bad,&process)==FNV_CPU_INVALID && !process);
    bad=cfg;bad.heap_bytes=0;assert(fnv_windows_create(&bad,&process)==FNV_CPU_INVALID && !process);
    bad=cfg;bad.heap_address=BASE+ARENA-8;assert(fnv_windows_create(&bad,&process)==FNV_CPU_INVALID && !process);
    write32(cfg.peb_address+0x18,1);assert(fnv_windows_create(&cfg,&process)==FNV_CPU_INVALID && !process);write32(cfg.peb_address+0x18,0);
    assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK);assert(read32(cfg.peb_address+0x18)==HEAP_BASE);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==8);
    struct fnv_windows_thread_config t=thread_config(0),bad_thread=t;bad_thread.stack_low=HEAP_BASE;bad_thread.stack_high=HEAP_BASE+4096;bad_thread.state.gpr[4]=HEAP_BASE+128;
    assert(fnv_windows_thread_attach(process,&bad_thread)==FNV_CPU_INVALID && fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1BF00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
        uint32_t args[]={0x401500,HEAP_BASE,4,32};for(unsigned j=0;j<4;++j)write32(s.gpr[4]+j*4,args[j]);struct fnv_cpu_fault fault;
        assert(fnv_cpu_execute(0x401520,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED && strstr(fault.text,"flags/SEH"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);puts("strict PASS");return 0;}
    uint32_t heap=call(PROCESS_HEAP,0,0,0,0);assert(heap==HEAP_BASE);call(SET_ERROR,0x1234,0,0,0);
    HANDLE oracle=HeapCreate(0,0,0);assert(oracle);unsigned cases=0;unsigned seed=0x8324;
    for(unsigned n=0;n<600;++n){seed=seed*1664525u+1013904223u;unsigned size=seed%2048;
        uint32_t a=call(ALLOC,heap,8,size,0);unsigned char *native=HeapAlloc(oracle,HEAP_ZERO_MEMORY,size);assert(a && !(a&7) && native);
        assert(call(HEAP_SIZE,heap,0,a,0)==size && HeapSize(oracle,0,native)>=size);
        assert(!memcmp(arena+a-BASE,native,size));memset(arena+a-BASE,0x53,size);memset(native,0x53,size);
        unsigned next=size+257;uint32_t b=call(REALLOC,heap,8,a,next);native=HeapReAlloc(oracle,HEAP_ZERO_MEMORY,native,next);assert(b && native);
        assert(!memcmp(arena+b-BASE,native,next) && call(VALIDATE,heap,0,b,0));
        assert(call(FREE,heap,0,b,0) && HeapFree(oracle,0,native));cases++;}
    assert(call(GET_ERROR,0,0,0,0)==0x1234 && call(FREE,heap,0,0,0));
    uint32_t a=call(ALLOC,heap,0,16,0),b=call(ALLOC,heap,0,16,0);assert(a && b);memset(arena+a-BASE,0x42,16);
    assert(!call(REALLOC,heap,16,a,4096) && call(HEAP_SIZE,heap,0,a,0)==16 && arena[a-BASE]==0x42);
    uint32_t moved=call(REALLOC,heap,8,a,4096);assert(moved && moved!=a && arena[moved-BASE]==0x42 && !arena[moved-BASE+16]);
    assert(!call(REALLOC,heap,8,moved,UINT32_MAX) && call(HEAP_SIZE,heap,0,moved,0)==4096);
    assert(call(REALLOC,heap,0,moved,7)==moved && call(HEAP_SIZE,heap,0,moved,0)==7);
    assert(call(REALLOC,heap,8|16,moved,20)==moved && arena[moved-BASE+6]==0x42);
    for(unsigned i=7;i<20;++i)assert(!arena[moved-BASE+i]);
    assert(call(FREE,heap,0,b,0) && call(FREE,heap,0,moved,0));
    uint32_t blocks[128];for(unsigned i=0;i<128;++i){blocks[i]=call(ALLOC,heap,0,1,0);assert(blocks[i]);}
    assert(!call(ALLOC,heap,0,1,0));for(unsigned i=0;i<128;++i)assert(call(FREE,heap,0,blocks[i],0));
    uint32_t full=call(ALLOC,heap,0,HEAP_BYTES-8,0);assert(full && !call(ALLOC,heap,0,1,0));assert(call(FREE,heap,0,full,0));
    assert(!call(VALIDATE,heap,0,full,0) && !call(ALLOC,heap+8,0,8,0));
    HANDLE other=CreateThread(NULL,0,worker,NULL,0,NULL);assert(other);
    for(unsigned i=0;i<1000;++i){uint32_t p=call(ALLOC,heap,8,96,0);assert(p);memset(arena+p-BASE,0x28,96);assert(call(FREE,heap,0,p,0));}
    assert(WaitForSingleObject(other,INFINITE)==WAIT_OBJECT_0);CloseHandle(other);assert(call(VALIDATE,heap,0,0,0));
    assert(HeapDestroy(oracle) && fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(!read32(cfg.peb_address+0x18));assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"heap_differential_sequences\":%u,\"concurrent_cycles\":2000,\"iat_imports\":8,\"bounded_heap\":true,\"native_engine_started\":false}\n",cases);return 0;
}
