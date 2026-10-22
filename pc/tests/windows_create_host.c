#include <windows.h>
#include "windows_thread.h"
#include "windows_user32.h"
#include "windows_boot_ui.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define CLASS (BASE+0x80000)
#define NAME (BASE+0x81000)
#define TITLE (BASE+0x82000)
static unsigned char *arena;
static struct fnv_windows_boot_ui *ui;
static unsigned failure,guest_count,oracle_count,creates,destroys;
static uint32_t guest_msgs[8],oracle_msgs[8];
static int backend_fail;
static fnv_cpu_function default_proc;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static int is_lifecycle(UINT msg){return msg==WM_NCCREATE || msg==WM_CREATE || msg==WM_DESTROY || msg==WM_NCDESTROY;}
static LRESULT CALLBACK oracle(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(is_lifecycle(msg)){
        assert(oracle_count<8);oracle_msgs[oracle_count++]=msg;
        if(msg==WM_NCCREATE && failure==1)return FALSE;
        if(msg==WM_CREATE && failure==2)return -1;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static int native_create(void *context,const struct fnv_boot_window_request *r,void **out){
    (void)context;*out=NULL;if(backend_fail)return 0;
    assert(r->width==320 && r->height==240 && r->style==WS_VISIBLE && !strcmp(r->title,"Synthetic window"));
    HWND hwnd=CreateWindowExA(0,"STATIC",r->title,WS_POPUP,0,0,320,240,NULL,NULL,GetModuleHandleA(NULL),NULL);
    assert(hwnd && IsWindow(hwnd));(void)SetActiveWindow(hwnd);*out=hwnd;creates++;return 1;
}
static int native_active(void *context,void *window){(void)context;return GetActiveWindow()==window;}
static int native_focus(void *context,void *window){(void)context;return GetForegroundWindow()==window;}
static void native_destroy(void *context,void *window){(void)context;assert(IsWindow(window) && DestroyWindow(window));destroys++;}
static void observe(void){
    uint32_t hwnd=fnv_cpu_argument_u32(0),msg=fnv_cpu_argument_u32(1),wp=fnv_cpu_argument_u32(2),lp=fnv_cpu_argument_u32(3);
    assert(guest_count<8 && is_lifecycle(msg) && !wp);guest_msgs[guest_count++]=msg;
    void *native;assert(fnv_windows_boot_ui_presentation(ui,hwnd,&native)==FNV_CPU_OK && IsWindow(native));
    if(msg==WM_NCCREATE || msg==WM_CREATE){
        uint32_t expected[]={0x12345678,BASE,0,0,240,320,0,0,WS_VISIBLE,TITLE,NAME,0};
        assert(!memcmp(arena+lp-BASE,expected,sizeof(expected)));
    }else assert(!lp);
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    uint32_t frame=s.gpr[4]-64,token=fnv_cpu_import_token(default_proc);assert(token);
    put(frame,0x401500);put(frame+4,hwnd);put(frame+8,msg);put(frame+12,wp);put(frame+16,lp);
    uint32_t result=fnv_cpu_invoke_stdcall(token,frame,16);assert(result==(msg==WM_NCCREATE));
    if(msg==WM_NCCREATE && failure==1)result=0;
    if(msg==WM_CREATE && failure==2)result=UINT32_MAX;
    fnv_cpu_return_u32(result,16);
}
static uint32_t call(unsigned api,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put(s.gpr[4],0x401500);
    for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*96,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"window call %u: %s\n",api,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
static DWORD WINAPI other_queue(void *context){
    struct fnv_windows_process *process=context;struct fnv_windows_thread_config t={0};
    t.thread_id=23;t.teb_address=BASE+0x20000;t.tls_expansion_address=BASE+0x21000;t.stack_low=BASE+0x22000;t.stack_high=BASE+0x2a000;
    fnv_cpu_state_init(&t.state,t.stack_high-256,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    put(t.state.gpr[4],0x401500);struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401500+8*96,&fault)==FNV_CPU_OK);
    struct fnv_cpu_state after;assert(fnv_cpu_get_state(&after)==FNV_CPU_OK && !after.gpr[0]);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_windows_user32 *desktop;assert(fnv_windows_user32_create(process,&desktop)==FNV_CPU_OK);
    assert(fnv_windows_boot_ui_create(process,&image,&ui)==FNV_CPU_OK);
    struct fnv_boot_window_backend backend={NULL,native_create,native_destroy,native_active,native_focus};
    if(argc==2 || strcmp(argv[2],"--strict-no-backend"))assert(fnv_windows_boot_ui_set_backend(ui,desktop,&backend)==FNV_CPU_OK);
    size_t n,bound;struct fnv_cpu_export exports[16];const struct fnv_cpu_export *e=fnv_windows_boot_ui_exports(&n);memcpy(exports,e,n*sizeof(*e));
    for(size_t i=0;i<n;i++)if(!strcmp(e[i].name,"DefWindowProcA"))default_proc=e[i].function;assert(default_proc);
    exports[n++]=(struct fnv_cpu_export){"test-ui.dll","Observe",0,0,observe};
    size_t count; e=fnv_windows_user32_exports(&count);memcpy(exports+n,e,count*sizeof(*e));n+=count;
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==14);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    if(argc==3 && !strcmp(argv[2],"--strict-stack"))t.stack_low=BASE+0x1be70;
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(fnv_windows_boot_ui_set_backend(ui,desktop,&backend)==FNV_CPU_BUSY);
    strcpy((char *)arena+NAME-BASE,"Synthetic_Class");strcpy((char *)arena+TITLE-BASE,"Synthetic window");
    put(CLASS+4,0x401c00);put(CLASS+16,BASE);put(CLASS+36,NAME);
    int host_swap=GetSystemMetrics(SM_SWAPBUTTON);uint32_t setting[]={1},metric[]={23};
    assert(!call(9,setting,1,0) && call(10,metric,1,0));setting[0]=0;
    assert(call(9,setting,1,0)==1 && !call(10,metric,1,0) && GetSystemMetrics(SM_SWAPBUTTON)==host_swap);
    UINT host_double=GetDoubleClickTime();assert(call(11,NULL,0,0)==500);
    setting[0]=1;assert(call(12,setting,1,0) && call(11,NULL,0,0)==1);
    setting[0]=5001;assert(call(12,setting,1,0) && call(11,NULL,0,0)==5000);
    setting[0]=0;assert(call(12,setting,1,0) && call(11,NULL,0,0)==500 && GetDoubleClickTime()==host_double);
    uint32_t a[]={CLASS};assert(call(0,a,1,0));
    uint32_t create[]={0,NAME,TITLE,WS_VISIBLE,0,0,320,240,0,0,BASE,0x12345678};
    if(argc==3){
        if(!strcmp(argv[2],"--strict-style"))create[3]=WS_CAPTION;
        call(1,create,12,1);
    }else{
        WNDCLASSA wc={0};wc.lpfnWndProc=oracle;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="FNV_Native_Oracle";assert(RegisterClassA(&wc));
        for(failure=0;failure<3;failure++){
            guest_count=oracle_count=0;uint32_t hwnd=call(1,create,12,0);
            HWND other=CreateWindowExA(0,wc.lpszClassName,"Oracle",0,0,0,320,240,NULL,NULL,wc.hInstance,(void *)0x12345678);
            assert((hwnd!=0)==(other!=NULL));
            if(hwnd){
                void *backing=NULL;assert(fnv_windows_boot_ui_presentation(ui,hwnd,&backing)==FNV_CPU_OK);
                assert(GetActiveWindow()==backing && call(8,NULL,0,0)==hwnd);
                HANDLE child=CreateThread(NULL,0,other_queue,process,0,NULL);assert(child && WaitForSingleObject(child,5000)==WAIT_OBJECT_0 && CloseHandle(child));
                SetActiveWindow(other);assert(GetActiveWindow()==other && !call(8,NULL,0,0));
                SetActiveWindow(backing);assert(GetActiveWindow()==backing && call(8,NULL,0,0)==hwnd);
                assert(fnv_windows_boot_ui_input_focus(ui,hwnd)==(GetForegroundWindow()==backing));
                assert(!fnv_windows_boot_ui_input_focus(ui,0xdead));
                uint32_t find[]={NAME,TITLE};assert(call(6,find,2,0)==hwnd);
                uint32_t unreg[]={NAME,BASE};assert(!call(3,unreg,2,0) && call(4,NULL,0,0)==ERROR_CLASS_HAS_WINDOWS);
                uint32_t bounds[]={hwnd,BASE+0x83000};RECT expected;
                assert(GetClientRect(backing,&expected));
                uint32_t last_error=call(4,NULL,0,0);
                assert(call(13,bounds,2,0) && call(4,NULL,0,0)==last_error);
                assert(!memcmp(arena+bounds[1]-BASE,&expected,16));
                uint32_t destroy[]={hwnd};assert(call(2,destroy,1,0));assert(DestroyWindow(other));
                memset(arena+bounds[1]-BASE,0xa5,16);
                assert(!call(13,bounds,2,0) && call(4,NULL,0,0)==ERROR_INVALID_WINDOW_HANDLE);
                for(unsigned i=0;i<16;i++)assert(arena[bounds[1]-BASE+i]==0xa5);
                bounds[0]=0;
                assert(!call(13,bounds,2,0) && call(4,NULL,0,0)==ERROR_INVALID_WINDOW_HANDLE);
                for(unsigned i=0;i<16;i++)assert(arena[bounds[1]-BASE+i]==0xa5);
                void *native=(void *)1;assert(fnv_windows_boot_ui_presentation(ui,hwnd,&native)==FNV_CPU_INVALID && !native);
                assert(!call(6,find,2,0));
            }
            assert(guest_count==oracle_count && !memcmp(guest_msgs,oracle_msgs,guest_count*4));
        }
        backend_fail=1;guest_count=0;assert(!call(1,create,12,0) && !guest_count && call(4,NULL,0,0)==ERROR_NOT_ENOUGH_MEMORY);
        assert(creates==destroys && creates==3);
        uint32_t unreg[]={NAME,BASE};assert(call(3,unreg,2,0));assert(UnregisterClassA(wc.lpszClassName,wc.hInstance));
        /* A controlled fault/session teardown releases backing after detach,
         * without attempting guest messages outside an execution boundary. */
        backend_fail=0;guest_count=0;assert(call(0,a,1,0));assert(call(1,create,12,0));
        assert(creates==4 && destroys==3);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_boot_ui_destroy(ui)==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(creates==destroys);assert(VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"real_backing_windows\":4,\"lifecycle_oracle_cases\":3,\"owner_queue_isolated\":true,\"virtual_mouse_settings\":true,\"engine_started\":false}");return 0;
}
