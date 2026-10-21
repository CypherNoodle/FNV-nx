#include <windows.h>
#include "windows_thread.h"
#include "windows_user32.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static uint32_t query_title;
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static enum fnv_cpu_result call(unsigned api,uint32_t value,uint32_t *answer,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state before,after;assert(fnv_cpu_get_state(&before)==FNV_CPU_OK);
    before.gpr[4]=BASE+0x1BF00;assert(fnv_cpu_set_state(&before)==FNV_CPU_OK);
    write32(before.gpr[4],0x401500);write32(before.gpr[4]+4,value);if(api>=3)write32(before.gpr[4]+8,query_title);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[4]==before.gpr[4]+4+(api==1?0:api>=3?8:4));*answer=after.gpr[0];}
    return r;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_windows_user32 *desktop,*second=(void *)1;
    assert(fnv_windows_user32_create(process,&desktop)==FNV_CPU_OK);
    assert(fnv_windows_user32_create(process,&second)==FNV_CPU_BUSY && !second);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_user32_exports(&n);assert(n==2 && exports==fnv_windows_user32_exports(NULL));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==5);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);uint32_t answer=0;
    if(argc==3 && !strncmp(argv[2],"--strict-username-",18)){
        query_title=BASE+0x82001;write32(query_title,32);uint32_t at=BASE+0x83000;
        if(!strcmp(argv[2],"--strict-username-size"))query_title=BASE+BYTES;else at=BASE+BYTES;
        struct fnv_cpu_fault fault;assert(call(4,at,&answer,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
        puts("strict PASS");return 0;
    }
    if(argc==3 && !strncmp(argv[2],"--strict-find-",14)){
        uint32_t at=BASE+0x80000;memset(arena+0x80000,'x',256);
        if(!strcmp(argv[2],"--strict-find-pointer"))at=BASE+BYTES;
        else if(!strcmp(argv[2],"--strict-find-atom"))at=7;
        else if(!strcmp(argv[2],"--strict-find-encoding")){arena[0x80000]=0xff;arena[0x80001]=0;}
        struct fnv_cpu_fault fault;assert(call(3,at,&answer,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    if(argc==3){uint32_t index=!strcmp(argv[2],"--strict-width")?0:!strcmp(argv[2],"--strict-height")?1:!strcmp(argv[2],"--strict-negative")?UINT32_MAX:0x1001;
        struct fnv_cpu_fault fault;assert(call(0,index,&answer,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fault.kind==FNV_CPU_UNIMPLEMENTED && fault.address==index && strstr(fault.text,"system metric pending"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);puts("strict PASS");return 0;}
    unsigned seed=0x62b8;
    for(unsigned i=0;i<1000;i++){seed=seed*1664525u+1013904223u;
        assert(call(2,seed,&answer,NULL)==FNV_CPU_OK);
        assert(call(0,0x1000,&answer,NULL)==FNV_CPU_OK && answer==0);
        assert(call(1,0,&answer,NULL)==FNV_CPU_OK && answer==seed);
    }
    /* Username is virtual; compare buffer/error rules without host identity. */
    char native_name[512];DWORD native_size=sizeof(native_name);
    assert(GetUserNameA(native_name,&native_size)); /* Initialize native security provider before checking LastError. */
    native_size=sizeof(native_name);SetLastError(0x1234);
    assert(GetUserNameA(native_name,&native_size) && GetLastError()==0x1234 && native_size==strlen(native_name)+1);
    DWORD small_size=0;assert(!GetUserNameA(NULL,&small_size) && GetLastError()==122 && small_size==native_size*2);
    query_title=BASE+0x82001;
    for(uint32_t capacity=0;capacity<9;capacity++){
        memset(arena+0x83000,0xa5,32);write32(query_title,capacity);
        assert(call(2,0x1234,&answer,NULL)==FNV_CPU_OK);
        assert(call(4,capacity?BASE+0x83000:0,&answer,NULL)==FNV_CPU_OK && answer==(capacity>=4));
        uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)arena[query_title-BASE+i]<<(8*i);
        assert(n==(capacity<4?8:4));
        if(capacity>=4){assert(!memcmp(arena+0x83000,"FNV",4) && arena[0x83004]==0xa5);}
        else for(unsigned i=0;i<32;i++)assert(arena[0x83000+i]==0xa5);
        assert(call(1,0,&answer,NULL)==FNV_CPU_OK && answer==(capacity<4?122:0x1234));
    }
    query_title=0;
    static const char class_name[]="FNV_SYNTHETIC_WINDOW_96d9b9a",title[]="Synthetic Window 96d9b9a";
    memcpy(arena+0x80000,class_name,sizeof(class_name));memcpy(arena+0x81000,title,sizeof(title));
    query_title=BASE+0x81000;
    assert(call(3,BASE+0x80000,&answer,NULL)==FNV_CPU_OK && !answer);
    uint32_t virtual_handle;assert(fnv_windows_user32_add_window(desktop,class_name,title,&virtual_handle)==FNV_CPU_OK && virtual_handle);
    assert(fnv_windows_user32_destroy(desktop)==FNV_CPU_BUSY);
    WNDCLASSA wc={0};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName=class_name;
    assert(RegisterClassA(&wc));HWND native_window=CreateWindowExA(0,class_name,title,0,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(native_window);
    for(unsigned pass=0;pass<3;pass++){
        if(pass==1){for(unsigned i=0;i<sizeof(class_name)-1;i++)if(arena[0x80000+i]>='A' && arena[0x80000+i]<='Z')arena[0x80000+i]+=32;
            for(unsigned i=0;i<sizeof(title)-1;i++)if(arena[0x81000+i]>='A' && arena[0x81000+i]<='Z')arena[0x81000+i]+=32;}
        uint32_t class_at=pass==2?0:BASE+0x80000;
        assert(FindWindowA(class_at?(char *)(arena+0x80000):NULL,(char *)(arena+0x81000))==native_window);
        assert(call(2,0x1234,&answer,NULL)==FNV_CPU_OK);
        assert(call(3,class_at,&answer,NULL)==FNV_CPU_OK && answer==virtual_handle);
        assert(call(1,0,&answer,NULL)==FNV_CPU_OK && answer==0x1234);
    }
    query_title=0;assert(call(3,BASE+0x80000,&answer,NULL)==FNV_CPU_OK && answer==virtual_handle);
    assert(fnv_windows_user32_remove_window(desktop,virtual_handle)==FNV_CPU_OK);
    assert(call(3,BASE+0x80000,&answer,NULL)==FNV_CPU_OK && !answer);
    assert(fnv_windows_user32_remove_window(desktop,virtual_handle)==FNV_CPU_INVALID);
    assert(DestroyWindow(native_window) && UnregisterClassA(class_name,wc.hInstance));
    uint32_t handles[16];for(unsigned i=0;i<16;i++)assert(fnv_windows_user32_add_window(desktop,class_name,title,handles+i)==FNV_CPU_OK && handles[i]!=virtual_handle);
    uint32_t excess=123;assert(fnv_windows_user32_add_window(desktop,class_name,title,&excess)==FNV_CPU_NO_MEMORY && !excess);
    for(unsigned i=0;i<16;i++)assert(fnv_windows_user32_remove_window(desktop,handles[i])==FNV_CPU_OK);
    SetLastError(0x1357);int native=GetSystemMetrics(SM_REMOTESESSION);DWORD error=GetLastError();
    /* Windows documents no extended error for this API. Initial lazy host
     * USER32 setup can change LastError; record it without assuming a value. */
    (void)error;
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"virtual_username_checks\":9,\"virtual_window_checks\":true,\"native_window_comparisons\":3,\"local_session_checks\":1000,\"last_error_preserved\":true,\"stdcall_checked\":true,\"windows_remote_session\":%d,\"native_engine_started\":false}\n",native);return 0;
}
