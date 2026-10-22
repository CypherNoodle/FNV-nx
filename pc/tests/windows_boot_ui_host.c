#include <windows.h>
#include "windows_thread.h"
#include "windows_boot_ui.h"
#include "pe_resources.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define CLASS_AT (BASE+0x80000)
#define NAME_AT (BASE+0x81000)
#define RECT_AT (BASE+0x82000)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(i*8));}
static uint32_t get(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)arena[at-BASE+i]<<(i*8);return v;}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,int negative){
    const unsigned counts[]={2,1,1,2,3,0,1};uint32_t args[]={a,b,c};struct fnv_cpu_state s,after;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<counts[api];i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"UI API %u: %s\n",api,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[4]==s.gpr[4]+4+counts[api]*4);return after.gpr[0];
}
static void init_class(uint32_t icon,uint32_t brush){
    memset(arena+CLASS_AT-BASE,0,40);put(CLASS_AT,3);put(CLASS_AT+4,0x401f00);put(CLASS_AT+16,BASE);
    put(CLASS_AT+20,icon);put(CLASS_AT+28,brush);put(CLASS_AT+36,NAME_AT);
    strcpy((char *)arena+NAME_AT-BASE,"FNV_Synthetic_Boot_Class");
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;struct fnv_windows_process *process;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_windows_boot_ui *ui,*other=(void *)1;
    struct fnv_native_image wrong=image;wrong.data++;
    assert(fnv_windows_boot_ui_create(process,&wrong,&other)==FNV_CPU_INVALID && !other);
    assert(fnv_windows_boot_ui_create(process,&image,&ui)==FNV_CPU_OK);
    assert(fnv_windows_boot_ui_create(process,&image,&other)==FNV_CPU_BUSY && !other);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_windows_boot_ui_exports(&n);assert(n==13);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==7);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);init_class(0,0);
    if(argc==3){const char *mode=argv[2];
        if(!strcmp(mode,"--strict-icon-name"))call(0,BASE,NAME_AT,0,1);
        else if(!strcmp(mode,"--strict-icon-module"))call(0,0,101,0,1);
        else if(!strcmp(mode,"--strict-stock"))call(1,13,0,0,1);
        else if(!strcmp(mode,"--strict-class-pointer"))call(2,BASE+BYTES-2,0,0,1);
        else if(!strcmp(mode,"--strict-class-procedure")){put(CLASS_AT+4,0x401800);call(2,CLASS_AT,0,0,1);}
        else if(!strcmp(mode,"--strict-empty-name")){arena[NAME_AT-BASE]=0;call(2,CLASS_AT,0,0,1);}
        else if(!strcmp(mode,"--strict-class-extra")){put(CLASS_AT+8,4);call(2,CLASS_AT,0,0,1);}
        else if(!strcmp(mode,"--strict-rectangle"))call(4,RECT_AT,WS_CAPTION,0,1);
        else {put(BASE+0x30a4,0xffff);call(0,BASE,101,0,1);}
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_boot_ui_destroy(ui)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    HMODULE native=LoadLibraryExA(argv[1],NULL,LOAD_LIBRARY_AS_DATAFILE|LOAD_LIBRARY_AS_IMAGE_RESOURCE);assert(native);
    HRSRC group_ref=FindResourceA(native,MAKEINTRESOURCEA(101),RT_GROUP_ICON);assert(group_ref);
    assert(SizeofResource(native,group_ref)==20);
    assert(!FindResourceA(native,MAKEINTRESOURCEA(102),RT_GROUP_ICON) && GetLastError()==ERROR_RESOURCE_NAME_NOT_FOUND);
    HRSRC icon_ref=FindResourceA(native,MAKEINTRESOURCEA(1),RT_ICON);assert(icon_ref);
    HGLOBAL icon_data=LoadResource(native,icon_ref);assert(icon_data);
    HICON native_icon=CreateIconFromResourceEx(LockResource(icon_data),SizeofResource(native,icon_ref),TRUE,0x30000,1,1,0);assert(native_icon);
    uint32_t icon=call(0,BASE,101,0,0);assert(icon && call(0,BASE,101,0,0)==icon);
    assert(!call(0,BASE,102,0,0) && call(5,0,0,0,0)==ERROR_RESOURCE_NAME_NOT_FOUND);
    uint32_t brush=call(1,4,0,0,0);assert(brush && call(1,4,0,0,0)==brush);
    LOGBRUSH lb;assert(GetObjectA(GetStockObject(BLACK_BRUSH),sizeof(lb),&lb)==sizeof(lb) && lb.lbStyle==BS_SOLID && lb.lbColor==RGB(0,0,0));
    init_class(icon,brush);WNDCLASSA wc={0};wc.style=3;wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.hIcon=native_icon;wc.hbrBackground=GetStockObject(BLACK_BRUSH);wc.lpszClassName=(char *)arena+NAME_AT-BASE;
    ATOM native_atom=RegisterClassA(&wc);assert(native_atom);uint32_t atom=call(2,CLASS_AT,0,0,0);assert(atom>=0xc000);
    struct fnv_boot_class cls;assert(fnv_windows_boot_ui_class(ui,atom,&cls)==FNV_CPU_OK && cls.procedure==0x401f00 && cls.icon==icon && cls.brush==brush && !strcmp(cls.name,wc.lpszClassName));
    assert(!RegisterClassA(&wc) && GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
    assert(!call(2,CLASS_AT,0,0,0) && call(5,0,0,0,0)==ERROR_CLASS_ALREADY_EXISTS);
    for(unsigned i=0;i<200;i++){
        int32_t rect[]={-(int32_t)i,(int32_t)i,320+(int32_t)i,240-(int32_t)i};memcpy(arena+RECT_AT-BASE,rect,sizeof(rect));RECT expected;memcpy(&expected,rect,sizeof(rect));
        DWORD style=i&1?WS_VISIBLE:WS_POPUP|WS_VISIBLE;assert(AdjustWindowRect(&expected,style,FALSE));
        call(6,0x1234,0,0,0);assert(call(4,RECT_AT,style,0,0) && !memcmp(&expected,arena+RECT_AT-BASE,16) && call(5,0,0,0,0)==0x1234);
    }
    assert(UnregisterClassA((LPCSTR)(uintptr_t)native_atom,wc.hInstance));assert(call(3,atom,BASE,0,0));
    assert(fnv_windows_boot_ui_class(ui,atom,&cls)==FNV_CPU_INVALID);
    assert(!call(3,atom,BASE,0,0) && call(5,0,0,0,0)==ERROR_CLASS_DOES_NOT_EXIST);
    /* Structural bounds and duplicate IDs must fail rather than reading a hole. */
    struct fnv_pe_resource resource;assert(fnv_pe_resource_find(&image,14,101,0x409,&resource)==FNV_RESOURCE_OK && resource.bytes==20);
    assert(!memcmp(resource.data,LockResource(LoadResource(native,group_ref)),20));
    assert(fnv_pe_resource_find(&image,3,1,0x409,&resource)==FNV_RESOURCE_OK && resource.bytes==48);
    assert(!memcmp(resource.data,LockResource(icon_data),48));
    uint32_t saved=get(BASE+0x3018);put(BASE+0x3018,3);
    assert(fnv_pe_resource_find(&image,3,1,0x409,&resource)==FNV_RESOURCE_INVALID);put(BASE+0x3018,saved);
    saved=get(BASE+0x3078);put(BASE+0x3078,0x40c);
    assert(fnv_pe_resource_find(&image,14,101,0x409,&resource)==FNV_RESOURCE_OK && resource.language==0x40c);put(BASE+0x3078,saved);
    saved=get(BASE+0x300c);put(BASE+0x300c,0xffff0000u);assert(fnv_pe_resource_find(&image,14,101,0x409,&resource)==FNV_RESOURCE_INVALID);put(BASE+0x300c,saved);
    saved=get(BASE+0x3090);put(BASE+0x3090,0xfffffff0u);assert(fnv_pe_resource_find(&image,14,101,0x409,&resource)==FNV_RESOURCE_INVALID);put(BASE+0x3090,saved);
    assert(fnv_windows_boot_ui_destroy(ui)==FNV_CPU_BUSY);
    assert(DestroyIcon(native_icon));assert(FreeLibrary(native));assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_boot_ui_destroy(ui)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"borderless_windows_comparisons\":200,\"icon_resource_checked\":true,\"class_lifecycle_checked\":true,\"no_window_created\":true,\"engine_started\":false}");return 0;
}
