#include <windows.h>
#include "windows_thread.h"
#include "windows_user32.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define MSG_AT (BASE+0x9000u)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t get(unsigned offset){unsigned char *p=arena+MSG_AT-BASE+offset;return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static enum fnv_cpu_result invoke(unsigned api,const uint32_t args[5],uint32_t *answer,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<5;i++)put(s.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+(api==0?20:api==2?4:0));*answer=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t at,uint32_t hwnd,uint32_t low,uint32_t high,uint32_t flags){
    uint32_t a[]={at,hwnd,low,high,flags},value=0;struct fnv_cpu_fault f;
    assert(invoke(api,a,&value,&f)==FNV_CPU_OK);return value;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_windows_user32 *desktop;assert(fnv_windows_user32_create(process,&desktop)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_user32_exports(&n);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={MSG_AT,0,0,0,1},answer=0;struct fnv_cpu_fault f;
        if(!strcmp(argv[2],"--strict-output"))args[0]=BASE+BYTES;
        else if(!strcmp(argv[2],"--strict-flags"))args[4]=0x10000;
        else if(!strcmp(argv[2],"--strict-window"))args[1]=0x1234;
        else args[2]=0x10000;
        assert(invoke(0,args,&answer,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    MSG native;PeekMessageA(&native,(HWND)-1,0,0,PM_NOREMOVE);unsigned comparisons=0;
    for(unsigned i=0;i<500;i++){
        struct fnv_user32_thread_message m={WM_APP+0x210+i%31,i*3571u,~i,12345+i,-15,(int32_t)i};
        assert(fnv_windows_user32_post_thread(desktop,22,&m)==FNV_CPU_OK);
        assert(PostThreadMessageA(GetCurrentThreadId(),m.message,m.wparam,(LPARAM)(int32_t)m.lparam));
        for(unsigned pass=0;pass<4;pass++){
            unsigned flags=pass<2?PM_NOREMOVE:PM_REMOVE;uint32_t hwnd=i&1?UINT32_MAX:0;
            memset(&native,0xa5,sizeof(native));memset(arena+MSG_AT-BASE,0xa5,32);
            SetLastError(0x1234);BOOL expected=PeekMessageA(&native,hwnd?(HWND)-1:NULL,WM_APP+0x200,WM_APP+0x300,flags);DWORD error=GetLastError();
            call(2,0x1234,0,0,0,0);uint32_t actual=call(0,MSG_AT,hwnd,WM_APP+0x200,WM_APP+0x300,flags);
            assert(!!actual==!!expected && call(1,0,0,0,0,0)==error);
            if(actual){assert(!get(0) && get(4)==native.message && get(8)==(uint32_t)native.wParam && get(12)==(uint32_t)native.lParam);
                assert(get(16)==m.time && get(20)==(uint32_t)m.x && get(24)==(uint32_t)m.y && arena[MSG_AT-BASE+28]==0xa5);}
            else for(unsigned j=0;j<28;j++)assert(arena[MSG_AT-BASE+j]==0xa5);
            comparisons++;
        }
    }
    /* Filtering leaves unmatched messages queued; WM_QUIT bypasses range. */
    struct fnv_user32_thread_message m={WM_APP+7,7,9,0,0,0};
    assert(fnv_windows_user32_post_thread(desktop,22,&m)==FNV_CPU_OK);
    assert(!call(0,MSG_AT,0,WM_APP+8,WM_APP+9,1));assert(call(0,MSG_AT,0,WM_APP+7,WM_APP+7,1));
    m.message=WM_QUIT;m.wparam=37;assert(fnv_windows_user32_post_thread(desktop,22,&m)==FNV_CPU_OK);PostQuitMessage(37);
    assert(PeekMessageA(&native,(HWND)-1,WM_APP,WM_APP+1,PM_REMOVE) && call(0,MSG_AT,UINT32_MAX,WM_APP,WM_APP+1,1));assert(get(4)==native.message && get(8)==native.wParam);comparisons++;
    m.message=WM_APP+1;assert(fnv_windows_user32_post_thread(desktop,23,&m)==FNV_CPU_OK);assert(!call(0,MSG_AT,0,0,0,1));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);t.thread_id=23;assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);assert(call(0,MSG_AT,0,0,0,1));
    for(unsigned i=0;i<256;i++){m.wparam=i;assert(fnv_windows_user32_post_thread(desktop,23,&m)==FNV_CPU_OK);}
    assert(fnv_windows_user32_post_thread(desktop,23,&m)==FNV_CPU_NO_MEMORY);
    for(unsigned i=0;i<256;i++)assert(call(0,MSG_AT,0,0,0,1) && get(8)==i);
    assert(!call(0,MSG_AT,0,0,0,1));
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_oracle_comparisons\":%u,\"thread_isolation\":true,\"queue_capacity\":256,\"native_engine_started\":false}\n",comparisons);return 0;
}
