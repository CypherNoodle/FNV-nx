#include <windows.h>
#include "fnv_sdk_runtime.h"
#include "windows_thread.h"
#include "guest_cpuid.h"
#include "windows_memory.h"
#include "windows_modules.h"
#include "windows_user32.h"
#include "windows_boot_ui.h"
#include "windows_debug.h"
#include "windows_com.h"
#include "windows_multimedia.h"
#include "windows_display.h"
#include "windows_display_win32.h"
#include "dinput8.h"
#include "dsound8.h"
#ifdef FNV_STARTUP_VULKAN
#include "d3d9_vk_backend.h"
#ifdef FNV_STARTUP_SDP
#include "d3d9_sdp.h"
#endif
#ifdef FNV_STARTUP_PROGRAM_HEADER
#include FNV_STARTUP_PROGRAM_HEADER
#endif
#include <stdio.h>
static uint32_t startup_graphics_query(void *context,uint32_t kind,const uint32_t *args,uint32_t count,void *out,uint32_t bytes){
#ifdef FNV_STARTUP_GRAPHICS_TRACE
    struct fnv_cpu_state state;uint32_t caller=0;
    if(fnv_cpu_get_state(&state)==FNV_CPU_OK){const unsigned char *p=fnv_cpu_try_memory(state.gpr[4],4);if(p)for(unsigned i=0;i<4;++i)caller|=(uint32_t)p[i]<<(8*i);}
    fprintf(stderr,"FNV_STARTUP_GRAPHICS kind=%u caller=%08x count=%u bytes=%u\n",kind,caller,count,bytes);
#endif
    if(kind==2 && count==2 && !args[0] && !(args[1]&~2u))return fnv_vk_backend_adapter_identifier(context,out,bytes);
    if(kind==0 && count==6)return fnv_vk_backend_check_format(context,args[3],args[4],args[5]);
    if(kind==1 && count==2 && !args[0] && args[1]==1)return fnv_vk_backend_device_caps(context,out,bytes);
    /* Unknown capabilities must not manufacture success. */
    return 0x8876086au;
}
#endif
#include "steam_callbacks.h"
#include "windows_registry.h"
#include "windows_system.h"
#include "windows_profiles.h"
#include "windows_shell.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdatomic.h>
#undef NDEBUG
#include <assert.h>
#include "startup_profile.h"
#ifdef FNV_CPU_X87_QUAD_MATH
#include "startup_math_state.h"
extern RECOMP_TLS unsigned fnv_probe_math_kind,fnv_probe_math_operation;
extern _Atomic unsigned fnv_probe_math_hits,fnv_probe_math_misses;
int fnv_startup_math_replay_open(const char *);
void fnv_startup_math_replay_close(void);
#endif
/* Desktop diagnostic uses a real hidden backing window. It never substitutes
 * the running Steam process or claims Horizon presentation. */
static int diagnostic_window_create(void *context,const struct fnv_boot_window_request *request,void **out){
    (void)context;*out=NULL;
    HWND window=CreateWindowExA(0,"STATIC",request->title,WS_POPUP,0,0,
        (int)request->width,(int)request->height,NULL,NULL,GetModuleHandleA(NULL),NULL);
    if(!window)return 0;
    if(request->style&WS_VISIBLE)(void)SetActiveWindow(window);
    *out=window;return 1;
}
static int diagnostic_window_active(void *context,void *window){(void)context;return GetActiveWindow()==window;}
static int diagnostic_window_focus(void *context,void *window){(void)context;return GetForegroundWindow()==window;}
static int diagnostic_input_focus(void *context,uint32_t hwnd){return fnv_windows_boot_ui_input_focus(context,hwnd);}
static int diagnostic_input_window_valid(void *context,uint32_t hwnd){void *window=NULL;
    return fnv_windows_boot_ui_presentation(context,hwnd,&window)==FNV_CPU_OK && IsWindow(window);}
static void diagnostic_window_destroy(void *context,void *window){(void)context;assert(DestroyWindow(window));}
#define BASE 0x400000u
static uint32_t arena_bytes;
#define ARENA arena_bytes
static unsigned char *arena;
RECOMP_TLS uint32_t fnv_probe_site;
static SRWLOCK fault_lock=SRWLOCK_INIT;
static struct fnv_cpu_fault first_fault;
static unsigned first_fault_live,first_fault_site,first_fault_thread;
static unsigned first_math_kind=UINT32_MAX,first_math_operation=UINT32_MAX;
/* Desktop diagnostic only: report unhandled host faults, then let Windows
 * terminate normally. Never convert native access violations to guest success. */
static LONG WINAPI report_host_exception(EXCEPTION_POINTERS *exception){
    uintptr_t image=(uintptr_t)GetModuleHandleA(NULL);
    EXCEPTION_RECORD *record=exception->ExceptionRecord;
    CONTEXT *c=exception->ContextRecord;
    fprintf(stderr,"FNV_HOST_EXCEPTION code=0x%08lx pc=0x%llx image_offset=0x%llx last_indirect_site=0x%08x thread=%lu\n",
        (unsigned long)record->ExceptionCode,(unsigned long long)c->Rip,
        (unsigned long long)(c->Rip-image),fnv_probe_site,(unsigned long)GetCurrentThreadId());
    if(record->NumberParameters>=2)fprintf(stderr,"access=%llu address=0x%llx\n",
        (unsigned long long)record->ExceptionInformation[0],(unsigned long long)record->ExceptionInformation[1]);
    fprintf(stderr,"host rsp=0x%llx rax=0x%llx rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx\n",
        (unsigned long long)c->Rsp,(unsigned long long)c->Rax,(unsigned long long)c->Rcx,
        (unsigned long long)c->Rdx,(unsigned long long)c->R8,(unsigned long long)c->R9);
    fprintf(stderr,"guest eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n",
        g_eax,g_ecx,g_edx,g_ebx,g_esp,g_ebp,g_esi,g_edi);
    fprintf(stderr,"guest trace:");for(unsigned i=0;i<16;i++)fprintf(stderr," %08x",fnv_cpu_icall_trace[(fnv_cpu_icall_trace_idx-1-i)%ICALL_TRACE_SIZE]);
    fprintf(stderr,"\n");unsigned char stack[48];SIZE_T got=0;
    void *source=fnv_cpu_try_memory(g_esp,sizeof(stack));
    if(source && ReadProcessMemory(GetCurrentProcess(),source,stack,sizeof(stack),&got) && got==sizeof(stack)){
        fprintf(stderr,"guest stack:");for(unsigned i=0;i<sizeof(stack);i++)fprintf(stderr,"%02x",stack[i]);fprintf(stderr,"\n");}
    fflush(stderr);return EXCEPTION_CONTINUE_SEARCH;
}
static void capture_fault(void *context,const struct fnv_cpu_fault *fault){
    (void)context;if(fault->kind==FNV_CPU_PEER_FAULT)return;
    AcquireSRWLockExclusive(&fault_lock);
    if(!first_fault_live){first_fault=*fault;first_fault_site=fnv_probe_site;first_fault_thread=GetCurrentThreadId();
#ifdef FNV_CPU_X87_QUAD_MATH
        first_math_kind=fnv_probe_math_kind;first_math_operation=fnv_probe_math_operation;
#endif
        first_fault_live=1;}
    ReleaseSRWLockExclusive(&fault_lock);
}
static void put32(uint32_t at,uint32_t value){assert(at>=BASE && (uint64_t)at+4<=BASE+ARENA);for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t permission(void *context,void *address,uint32_t bytes,uint32_t value){
    (void)context;DWORD old;return VirtualProtect(address,bytes,value==3?PAGE_READWRITE:value==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();
}
static int same(const struct fnv_cpu_export *e,const struct fnv_native_import *i){
    return !_stricmp(e->dll,i->dll) && e->by_ordinal==i->by_ordinal &&
        (i->by_ordinal?e->ordinal==i->ordinal:!strcmp(e->name,i->name));
}
static void string(const char *s){putchar('"');for(;*s;s++){unsigned c=(unsigned char)*s;if(c=='"' || c=='\\')putchar('\\');
    if(c>=32 && c<127)putchar((int)c);else printf("\\u%04x",c);}putchar('"');}
static uint32_t number(const char *text){
    assert(text && text[0]>='0' && text[0]<='9');errno=0;char *end;
    unsigned long long n=strtoull(text,&end,10);assert(!errno && !*end && n<=UINT32_MAX);return (uint32_t)n;
}
static int peek(uint32_t at,void *out,size_t bytes){
    SIZE_T got=0;
    return at>=BASE && (uint64_t)at+bytes<=(uint64_t)BASE+ARENA &&
        ReadProcessMemory(GetCurrentProcess(),arena+(at-BASE),out,bytes,&got) && got==bytes;
}
static void diagnostic_string(uint32_t at){
    char text[512];size_t n=0;const char *status="truncated";
    for(;n<sizeof(text)-1;n++){
        unsigned char c;
        if((uint64_t)at+n>UINT32_MAX || !peek(at+(uint32_t)n,&c,1)){status="unreadable";break;}
        if(!c){status="complete";break;}text[n]=(char)c;
    }
    text[n]=0;printf("{\"address\":%u,\"status\":",at);string(status);printf(",\"value\":");string(text);putchar('}');
}
static void message_box(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"USER32.dll!MessageBoxA"))return;
    uint32_t words[5];printf(",\"message_box\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"return_address\":%u,\"window\":%u,\"type\":%u,\"text\":",words[0],words[1],words[4]);
    diagnostic_string(words[2]);printf(",\"caption\":");diagnostic_string(words[3]);putchar('}');
}
static void exception_request(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"KERNEL32.dll!RaiseException"))return;
    uint32_t words[5],args[15];printf(",\"exception_request\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"readable\":false}");return;}
    printf("\"code\":%u,\"flags\":%u,\"argument_count\":%u,\"arguments\":[",words[1],words[2],words[3]);
    int readable=words[3]<=15 && (!words[3] || peek(words[4],args,words[3]*4));
    if(readable)for(unsigned i=0;i<words[3];i++)printf("%s%u",i?",":"",args[i]);
    printf("],\"readable\":%s",readable?"true":"false");
    if(readable && words[1]==0x406d1388 && words[3]==4){printf(",\"thread_name\":");diagnostic_string(args[1]);}
    putchar('}');
}
static void profile_request(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"KERNEL32.dll!GetPrivateProfileIntA") && strncmp(fault->text,"Windows profile",15))return;
    if(!strcmp(fault->text,"Windows profile string path unsupported")){
        uint32_t words[7];printf(",\"ini_string_request\":{");
        if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
        printf("\"arguments_readable\":true,\"section\":");diagnostic_string(words[1]);printf(",\"key\":");diagnostic_string(words[2]);
        printf(",\"default\":");diagnostic_string(words[3]);printf(",\"output\":%u,\"capacity\":%u,\"file\":",words[4],words[5]);diagnostic_string(words[6]);putchar('}');return;
    }
    uint32_t words[5];printf(",\"ini_request\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"default\":%u,\"section\":",words[3]);diagnostic_string(words[1]);
    printf(",\"key\":");diagnostic_string(words[2]);printf(",\"file\":");diagnostic_string(words[4]);putchar('}');
}
static void shell_folder_request(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"SHELL32.dll!SHGetFolderPathA") && strcmp(fault->text,"Windows SHGetFolderPathA variant pending"))return;
    uint32_t words[6];printf(",\"shell_folder_request\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"return_address\":%u,\"hwnd\":%u,\"csidl\":%u,\"token\":%u,\"flags\":%u,\"path_buffer\":%u}",
        words[0],words[1],words[2],words[3],words[4],words[5]);
}
static void file_delete_request(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"KERNEL32.dll!DeleteFileA"))return;
    uint32_t words[2];printf(",\"file_delete_request\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"return_address\":%u,\"path\":",words[0]);
    diagnostic_string(words[1]);putchar('}');
}
static void shell_execute_request(const struct fnv_cpu_fault *fault){
    if(strcmp(fault->text,"SHELL32.dll!ShellExecuteA"))return;
    uint32_t words[7];printf(",\"shell_execute\":{");
    if(!peek(fault->state.gpr[4],words,sizeof(words))){printf("\"arguments_readable\":false}");return;}
    printf("\"arguments_readable\":true,\"window\":%u,\"show\":%u,\"operation\":",words[1],words[6]);diagnostic_string(words[2]);
    printf(",\"file\":");diagnostic_string(words[3]);printf(",\"parameters\":");diagnostic_string(words[4]);printf(",\"directory\":");diagnostic_string(words[5]);printf("}");
}
static void import_call_arguments(const struct fnv_cpu_fault *fault){
    if(!strstr(fault->text,".dll!"))return;
    uint32_t words[9];unsigned count=0;
    for(;count<9;count++){uint64_t at=(uint64_t)fault->state.gpr[4]+count*4u;
        if(at>UINT32_MAX || !peek((uint32_t)at,words+count,4))break;}
    printf(",\"import_call_arguments\":{\"stack_dwords\":[");
    for(unsigned i=0;i<count;i++)printf("%s%u",i?",":"",words[i]);
    printf("],\"possible_strings\":[");unsigned emitted=0;
    for(unsigned i=1;i<count;i++){unsigned char first;
        if(!peek(words[i],&first,1) || (first && (first<32 || first>=127)))continue;
        printf("%s{\"argument\":%u,\"string\":",emitted++?",":"",i-1);
        diagnostic_string(words[i]);putchar('}');}
    printf("]}");
}
static void guest_stack_words(const struct fnv_cpu_fault *fault){
    uint32_t esp=fault->state.gpr[4],ebp=fault->state.gpr[5],words[8];
    printf(",\"guest_stack\":{\"esp\":%u,\"esp_dwords\":[",esp);
    unsigned count=0;
    for(;count<8;count++){
        uint64_t at=(uint64_t)esp+count*4u;
        if(at>UINT32_MAX || !peek((uint32_t)at,words+count,4))break;
    }
    for(unsigned i=0;i<count;i++)printf("%s%u",i?",":"",words[i]);
    printf("],\"ebp\":%u,\"ebp_frame\":[",ebp);
    uint32_t frame[6];
    int frame_readable=ebp>=8 && peek(ebp-8,frame,sizeof(frame));
    if(frame_readable)for(unsigned i=0;i<6;i++)printf("%s%u",i?",":"",frame[i]);
    printf("],\"ebp_frame_readable\":%s}",frame_readable?"true":"false");
}
int main(int argc,char **argv){
    SetUnhandledExceptionFilter(report_host_exception);
    assert(argc==8+FNV_STARTUP_CAPTURED_TEXT);
    arena_bytes=number(argv[argc-5]);uint32_t vm_bytes=number(argv[argc-4]),commit_limit=number(argv[argc-3]);
    uint32_t worker_capacity=number(argv[argc-1]);assert(worker_capacity==0 || worker_capacity==4);
#ifdef FNV_CPU_X87_QUAD_MATH
    assert(fnv_startup_math_replay_open(argv[argc-2]));
#else
    assert(!argv[argc-2][0]);
#endif
    assert(arena_bytes>0x2800000 && (uint64_t)BASE+arena_bytes<=UINT32_MAX &&
        vm_bytes==arena_bytes-0x2800000 && commit_limit && commit_limit<=vm_bytes &&
        !(arena_bytes&65535) && !(commit_limit&65535));
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
#if FNV_STARTUP_CAPTURED_TEXT
    assert(FNV_STARTUP_TEXT_RVA<image.size && FNV_STARTUP_TEXT_BYTES<=image.size-FNV_STARTUP_TEXT_RVA);
    unsigned char *text=fnv_native_guest_range(&image,image.base+FNV_STARTUP_TEXT_RVA,FNV_STARTUP_TEXT_BYTES);assert(text);
    FILE *captured=fopen(argv[3],"rb");assert(captured);
    assert(fread(text,1,FNV_STARTUP_TEXT_BYTES,captured)==FNV_STARTUP_TEXT_BYTES && fgetc(captured)==EOF && !ferror(captured));
    assert(fclose(captured)==0);
#endif
    struct fnv_vm_permissions permissions={NULL,permission};struct fnv_guest_memory *vm;
    struct fnv_vm_config v={arena+0x2800000,BASE+0x2800000,vm_bytes,commit_limit,&permissions,fnv_vm_eager_apply};
    assert(!fnv_vm_create(&v,&vm).error);
    struct fnv_windows_config c={0};c.cpu.cpuid=fnv_guest_cpuid_limits;c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.diagnostic=capture_fault;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=0x1711;c.peb_address=BASE+0x2000000;
    c.memory=vm;c.heap_address=BASE+0x2400000;c.heap_bytes=0x400000;c.heap_capacity=65536;c.private_heap_reserve=0x1000000;c.private_heap_limit=8;
    c.parameters_address=BASE+0x2100000;c.parameters_bytes=0x10000;c.command_line="FalloutNV.exe";c.image_path="FalloutNV.exe";
    c.kernel_handle_capacity=1024;c.critical_section_capacity=256;c.file_root=argv[2];c.file_handle_capacity=128;
    if(worker_capacity){
        c.worker_address=BASE+0x2250000;c.worker_bytes=0x100000;c.worker_capacity=worker_capacity;
        c.worker_stack_bytes=0x20000;c.worker_tls_bytes=0x11000;c.worker_host_stack_bytes=0x100000;
    }
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_display_backend display_backend={NULL,fnv_display_enumerate_win32};
    struct fnv_windows_display *display;assert(fnv_windows_display_create(process,&display_backend,&display)==FNV_CPU_OK);
    struct fnv_windows_boot_ui *boot_ui;assert(fnv_windows_boot_ui_create(process,&image,&boot_ui)==FNV_CPU_OK);
    struct fnv_user32_metrics metrics={NULL,fnv_display_metrics_win32};
    struct fnv_windows_user32 *user32;assert(fnv_windows_user32_create_with_metrics(process,&metrics,&user32)==FNV_CPU_OK);
    struct fnv_boot_window_backend window_backend={NULL,diagnostic_window_create,diagnostic_window_destroy,diagnostic_window_active,diagnostic_window_focus};
    assert(fnv_windows_boot_ui_set_backend(boot_ui,user32,&window_backend)==FNV_CPU_OK);
    struct fnv_vm_result input_pool=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!input_pool.error);
    struct fnv_dinput8_config input_config={0};input_config.instance=BASE;input_config.objects_address=input_pool.value;input_config.objects_bytes=65536;
    input_config.keyboard=input_config.mouse=1;input_config.memory=vm;input_config.context=boot_ui;input_config.valid_window=diagnostic_input_window_valid;input_config.has_input_focus=diagnostic_input_focus;
    struct fnv_dinput8 *dinput;assert(fnv_dinput8_create(process,&input_config,&dinput)==FNV_CPU_OK);
    struct fnv_vm_result sound_pool=fnv_vm_allocate(vm,0,4*1024*1024,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!sound_pool.error);
    struct fnv_pcm_queue *sound_queue;assert(fnv_pcm_create(2048,&sound_queue)==FNV_PCM_OK);
    struct fnv_dsound8_config sound_config={0};sound_config.arena_address=sound_pool.value;sound_config.arena_bytes=4*1024*1024;
    sound_config.memory=vm;sound_config.output=sound_queue;sound_config.context=boot_ui;sound_config.valid_window=diagnostic_input_window_valid;
    struct fnv_dsound8 *sound;assert(fnv_dsound8_create(process,&sound_config,&sound)==FNV_CPU_OK);
    /* Headless desktop diagnostic sink only; no audout or native playback. */
    /* Neutral controlled snapshots, not physical desktop input or HW evidence. */
    struct fnv_steam_callbacks *steam_callbacks;assert(fnv_steam_callbacks_create(process,&steam_callbacks)==FNV_CPU_OK);
    struct fnv_windows_system *system;assert(fnv_windows_system_create(process,3,&system)==FNV_CPU_OK);
#ifdef FNV_STARTUP_VULKAN
    HMODULE vulkan_loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(vulkan_loader);
    FARPROC vulkan_symbol=GetProcAddress(vulkan_loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr vulkan_gp;memcpy(&vulkan_gp,&vulkan_symbol,sizeof(vulkan_gp));assert(vulkan_gp);
    /* Prepared real offscreen device; not connected to WSI or game presentation. */
    struct fnv_vk_config graphics_config={1280,720,128,2,128,128u<<20,1};struct fnv_vk_backend *graphics;VkResult graphics_failure;
    assert(!fnv_vk_backend_create(vulkan_gp,&graphics_config,&graphics,&graphics_failure));
    assert(!fnv_vk_backend_draw_wait(graphics,10000000000ull));
    struct fnv_vm_result graphics_objects=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!graphics_objects.error);
    struct fnv_vm_result graphics_buffers=fnv_vm_allocate(vm,0,16u<<20,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!graphics_buffers.error);
    struct fnv_d3d9_factory_profile graphics_profile={1280,720,21,75,60,graphics,startup_graphics_query};
    struct fnv_d3d9_com_config graphics_com_config={0};graphics_com_config.objects_address=graphics_objects.value;graphics_com_config.objects_bytes=65536;
    graphics_com_config.resource_capacity=128;graphics_com_config.vertex_floats=256;graphics_com_config.pixel_floats=224;graphics_com_config.memory=vm;
    graphics_com_config.buffer_address=graphics_buffers.value;graphics_com_config.buffer_bytes=16u<<20;graphics_com_config.factory=&graphics_profile;
    fnv_vk_backend_callbacks(graphics,&graphics_com_config.backend);
#ifdef FNV_STARTUP_SDP
    /* Optional original user package, never a substitute for prepared SPIR-V.
     * The bytecode whitelist does not enable shader caps or backend programs. */
    const char *sdp_path=getenv("FNV_STARTUP_SDP_PATH");struct fnv_d3d9_sdp *sdp=NULL;
    if(sdp_path){
        assert(fnv_sdp_load(sdp_path,16u<<20,&sdp)==FNV_SDP_OK);
        graphics_com_config.catalog=fnv_sdp_entries(sdp,&graphics_com_config.catalog_count);
        fprintf(stderr,"FNV_STARTUP_SDP records=%zu translated=0\n",graphics_com_config.catalog_count);
    }
#endif
#ifdef FNV_STARTUP_PROGRAM_HEADER
    assert(!fnv_vk_prepare_program(graphics,&fnv_startup_program,graphics_com_config.catalog,graphics_com_config.catalog_count));
    fprintf(stderr,"FNV_STARTUP_PROGRAM shaders=2 linked_pairs=1 caps_unchanged=1\n");
#endif
    struct fnv_d3d9_com *graphics_com;assert(fnv_d3d9_com_create(fnv_windows_runtime(process),&graphics_com_config,&graphics_com)==FNV_CPU_OK);
#ifdef FNV_STARTUP_SDP
    fnv_sdp_destroy(sdp);
#endif
#endif
    struct fnv_cpu_export catalog[256],extras[FNV_NATIVE_MAX_IMPORTS];size_t known=0,extra_count=0,n;
    const struct fnv_cpu_export *e=fnv_windows_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;
    e=fnv_windows_modules_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_memory_exports(&n);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_dinput8_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_debug_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_com_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_multimedia_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_display_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_dsound8_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_boot_ui_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_user32_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_system_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_profile_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_shell_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_steam_callbacks_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
    e=fnv_windows_registry_exports(&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
#ifdef FNV_STARTUP_VULKAN
    e=fnv_d3d9_com_exports(graphics_com,&n);assert(known+n<=256 && extra_count+n<=FNV_NATIVE_MAX_IMPORTS);memcpy(catalog+known,e,n*sizeof(*e));known+=n;memcpy(extras+extra_count,e,n*sizeof(*e));extra_count+=n;
#endif
    struct fnv_windows_modules *modules;assert(fnv_windows_modules_create(process,&image,"FalloutNV.exe",catalog,known,&modules)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS],scratch[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&imported)==FNV_NATIVE_OK);
    assert(imported==sizeof(missing_imports)/sizeof(missing_imports[0]));unsigned supported=0;
    for(size_t i=0;i<imported;i++){
        int found=0;for(size_t j=0;j<known;j++)if(same(catalog+j,imports+i))found=1;
        if(found){supported++;continue;}
        for(size_t j=0;j<extra_count;j++)if(same(extras+j,imports+i))found=1;
        if(found)continue;
        extras[extra_count++]=(struct fnv_cpu_export){imports[i].dll,imports[i].by_ordinal?NULL:imports[i].name,imports[i].ordinal,imports[i].by_ordinal,missing_imports[i]};
    }
    size_t bound;assert(fnv_windows_bind_imports(process,&image,scratch,FNV_NATIVE_MAX_IMPORTS,extras,extra_count,&bound)==FNV_NATIVE_OK && bound==imported);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x2200000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=t.teb_address+0x4000;t.stack_high=t.teb_address+0x24000;t.static_tls_vector=t.teb_address+8192;t.static_tls_vector_bytes=4096;
    t.static_tls_data=t.teb_address+0x30000;t.static_tls_data_bytes=0x10000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);put32(t.state.gpr[4],0);
    struct fnv_cpu_fault fault={0};enum fnv_cpu_result result=fnv_cpu_execute(FNV_STARTUP_ENTRY,&fault);
    assert(result==FNV_CPU_OK || result==FNV_CPU_GUEST_FAULT || result==FNV_CPU_FAULTED);
    struct fnv_windows_worker_stats workers;
    for(unsigned attempt=0;;attempt++){
        assert(fnv_windows_worker_stats(process,&workers)==FNV_CPU_OK);
        if(!workers.active || attempt==2000)break;Sleep(1);
    }
    AcquireSRWLockShared(&fault_lock);
    if(first_fault_live){fault=first_fault;fnv_probe_site=first_fault_site;result=FNV_CPU_GUEST_FAULT;}
    ReleaseSRWLockShared(&fault_lock);
    struct fnv_windows_heap_stats heaps;assert(fnv_windows_heap_stats(process,&heaps)==FNV_CPU_OK);
    printf("{\"faulted\":%s,\"result\":%u,\"implemented_imports\":%u,\"trap_imports\":%u,\"fault_address\":%u,\"fault_kind\":%u,\"text\":",result==FNV_CPU_GUEST_FAULT?"true":"false",result,supported,(unsigned)imported-supported,fault.address,fault.kind);string(fault.text);
    printf(",\"last_indirect_site\":%u,\"trace_index\":%u,\"call_count\":%llu,\"registers\":[",fnv_probe_site,fault.trace_index,(unsigned long long)fault.call_count);
    for(unsigned i=0;i<8;i++)printf("%s%u",i?",":"",fault.state.gpr[i]);
    printf("],\"trace\":[");for(unsigned i=0;i<16;i++)printf("%s%u",i?",":"",fault.trace[i]);
    printf("],\"live_heaps\":%u,\"heap_blocks\":%u",heaps.live_heaps,heaps.live_blocks);
    struct fnv_vm_counters counters;fnv_vm_get_counters(vm,&counters);
    printf(",\"guest_memory\":{\"arena_bytes\":%u,\"vm_bytes\":%u,\"commit_limit\":%u,\"committed_bytes\":%u,\"reserved_bytes\":%u}",
        arena_bytes,vm_bytes,commit_limit,counters.committed_bytes,counters.reserved_bytes);
    struct fnv_windows_file_stats files;assert(fnv_windows_file_stats(process,&files)==FNV_CPU_OK);
    printf(",\"file_stats\":{\"reads\":%llu,\"read_bytes\":%llu,\"read_ns\":%llu,\"writes\":%llu,\"write_bytes\":%llu,\"write_ns\":%llu,\"syncs\":%llu,\"sync_ns\":%llu,\"sync_errors\":%llu,\"closes\":%llu,\"close_ns\":%llu,\"close_errors\":%llu}",
        (unsigned long long)files.reads,(unsigned long long)files.read_bytes,(unsigned long long)files.read_ns,
        (unsigned long long)files.writes,(unsigned long long)files.write_bytes,(unsigned long long)files.write_ns,
        (unsigned long long)files.syncs,(unsigned long long)files.sync_ns,(unsigned long long)files.sync_errors,
        (unsigned long long)files.closes,(unsigned long long)files.close_ns,(unsigned long long)files.close_errors);
    message_box(&fault);exception_request(&fault);profile_request(&fault);shell_folder_request(&fault);file_delete_request(&fault);shell_execute_request(&fault);import_call_arguments(&fault);guest_stack_words(&fault);
    printf(",\"worker_stats\":{\"capacity\":%u,\"active\":%u,\"finished\":%u,\"created\":%llu,\"reaped\":%llu,\"failures\":%llu,\"peer_stops\":%llu},\"origin_host_thread\":%u",
        workers.capacity,workers.active,workers.finished,(unsigned long long)workers.created,(unsigned long long)workers.reaped,(unsigned long long)workers.failures,(unsigned long long)workers.peer_stops,first_fault_thread);
#ifdef FNV_CPU_X87_QUAD_MATH
    printf(",\"math_replay\":{\"schema\":\"fnv-x87-exact-replay-v1\",\"hits\":%u,\"misses\":%u}",fnv_probe_math_hits,fnv_probe_math_misses);
    if(first_math_kind!=UINT32_MAX && !strncmp(fault.text,"Target ARM64 x87 ",sizeof("Target ARM64 x87 ")-1)){
        unsigned char bytes[FNV_STARTUP_MATH_STATE_BYTES];startup_math_pack(bytes,&fault.state.x87_extended);
        printf(",\"math_request\":{\"kind\":%u,\"operation\":%u,\"state_hex\":\"",first_math_kind,first_math_operation);
        for(unsigned i=0;i<sizeof(bytes);i++){printf("%02x",bytes[i]);}printf("\"}");
    }
    if(!workers.active)fnv_startup_math_replay_close();
#endif
    printf(",\"current_thread_COM_initialization_count\":%u",fnv_windows_com_initialization_count());
#ifdef FNV_STARTUP_VULKAN
    struct fnv_vk_stats graphics_stats;assert(!fnv_vk_backend_stats(graphics,&graphics_stats));
    printf(",\"native_vulkan_startup\":{\"headless\":true,\"draws\":%llu,\"submissions\":%llu}",(unsigned long long)graphics_stats.draws,(unsigned long long)graphics_stats.submissions);
#endif
    enum fnv_cpu_result detach=fnv_windows_thread_detach(),cleanup=FNV_CPU_BUSY;
    int graphics_clean=1;
#ifdef FNV_STARTUP_VULKAN
    graphics_clean=0;
    if(detach==FNV_CPU_OK && !workers.active && !fnv_vk_backend_wait(graphics,1000000000ull) &&
       fnv_d3d9_com_destroy(graphics_com)==FNV_CPU_OK && !fnv_vk_backend_destroy(graphics) &&
       !fnv_vm_free(vm,graphics_buffers.value,0,FNV_VM_RELEASE).error && !fnv_vm_free(vm,graphics_objects.value,0,FNV_VM_RELEASE).error){
        assert(FreeLibrary(vulkan_loader));graphics_clean=1;
    }
    printf(",\"native_vulkan_cleanup_complete\":%s",graphics_clean?"true":"false");
#endif
    if(detach==FNV_CPU_OK && !workers.active && graphics_clean && fnv_windows_modules_destroy(modules)==FNV_CPU_OK && fnv_windows_display_destroy(display)==FNV_CPU_OK && fnv_dinput8_destroy(dinput)==FNV_CPU_OK && fnv_dsound8_destroy(sound)==FNV_CPU_OK && !fnv_vm_free(vm,sound_pool.value,0,FNV_VM_RELEASE).error && !fnv_vm_free(vm,input_pool.value,0,FNV_VM_RELEASE).error && fnv_steam_callbacks_destroy(steam_callbacks)==FNV_CPU_OK && fnv_windows_system_destroy(system)==FNV_CPU_OK && fnv_windows_boot_ui_destroy(boot_ui)==FNV_CPU_OK && fnv_windows_user32_destroy(user32)==FNV_CPU_OK)cleanup=fnv_windows_destroy(process);
#ifdef FNV_STARTUP_VULKAN
    /* On teardown failure, retain remaining backing until process exit. */
#endif
    if(cleanup==FNV_CPU_OK)fnv_pcm_destroy(sound_queue);
    printf(",\"thread_detach_result\":%u,\"process_cleanup_result\":%u}\n",detach,cleanup);
    /* A fault can leave live guest private heaps. In that case this isolated
     * host process owns the remaining arena until OS exit; never free it while
     * its WindowsProcess still references it. This is not normal engine exit. */
    if(cleanup==FNV_CPU_OK){assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));}
    return 0;
}
