/* Private captured-PC engine experiment. Not a cold on-disk EXE boot.
 * engine_profile.h / engine_programs.h are generated private build inputs.
 * All backing survives until process exit; no partial teardown of live guest
 * threads or GPU work is attempted after a fault. */
#include <switch.h>
#include <malloc.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fnv_sdk_runtime.h"
#include "cpu_native_observer.h"
#include "engine_process_plan.h"
#include "engine_movie_io.h"
#include "engine_crash.h"
#include "engine_watchdog.h"
#include "windows_memory.h"
#include "windows_modules.h"
#include "windows_user32.h"
#include "windows_user32_input.h"
#include "windows_boot_ui.h"
#include "windows_boot_horizon.h"
#include "windows_debug.h"
#include "windows_d3dx.h"
#include "windows_com.h"
#include "windows_com_diag.h"
#include "windows_multimedia.h"
#include "windows_mmio.h"
#include "windows_vorbis.h"
#include "windows_display.h"
#include "windows_display_resolution.h"
#include "windows_strings.h"
#include "windows_filetime.h"
#include "windows_system.h"
#include "windows_profiles.h"
#include "windows_shell.h"
#include "windows_registry.h"
#include "steam_callbacks.h"
#include "dinput8_hid.h"
#include "media_horizon.h"
#include "dsound8.h"
#include "directshow_graph.h"
#include "bink_sound.h"
#include "bink_movie.h"
#include "host_clock.h"
#include "host_thread.h"
#include "host_mutex.h"
#include "d3d9_vk_horizon.h"
#include "d3d9_vk_metrics.h"
#include "d3d9_vk_progress.h"
#include "windows_diagnostics.h"
#include "guest_memory_diagnostics.h"
#include "d3d9_vk_prepared.h"
#include "d3d9_sdp.h"
#include "d3d9_com_diag.h"
#include "engine_profile.h"
#include "engine_programs.h"
#define ROOT "sdmc:/switch/FNV-nx"
#define REQUIRE(expr,label) do{if(!(expr)){report("FAIL %s\n",label);return 0;}}while(0)
static FILE *log_file;
static char import_labels[FNV_NATIVE_MAX_IMPORTS][96];
static fnv_host_mutex log_guard;
static struct fnv_engine_process_plan *plan;
static struct fnv_windows_process *process;
static struct fnv_guest_memory *vm;
static struct fnv_native_image image;
static struct fnv_windows_boot_ui *boot_ui;
static struct fnv_windows_user32 *user32;
static struct fnv_dinput8 *dinput;
static struct fnv_horizon_pad *controller;
static struct fnv_horizon_audio *audio;
static struct fnv_dsound8 *sound;
static struct fnv_vk_backend *graphics;
struct fnv_vk_backend *fnv_engine_watchdog_renderer(void){return graphics;}
static struct fnv_d3d9_com *graphics_com;
static struct fnv_display_resolution resolution;
static unsigned char adapter[1104];
static struct fnv_boot_horizon window_binding;
static struct fnv_vm_permissions permissions;
static atomic_int done,stop_requested;
static enum fnv_cpu_result execution_result;
static struct fnv_cpu_fault execution_fault;
static void report(const char *format,...) {
    fnv_host_mutex_lock(&log_guard);va_list args;va_start(args,format);
    vfprintf(log_file,format,args);va_end(args);fflush(log_file);fnv_host_mutex_unlock(&log_guard);
}
/* Real host half of D3DPRESENTFLAG_LOCKABLE_BACKBUFFER. Both directions use
 * the backend's fence-checked transfers; a failure reaches the guest as a lock
 * error, never as staged or fabricated pixels. */
static uint32_t surface_read(void *context,uintptr_t handle,void *out,size_t bytes,uint64_t timeout){
    return fnv_vk_backend_read_color(context,handle,out,bytes,timeout);
}
static uint32_t surface_write(void *context,uintptr_t handle,const void *in,size_t bytes,uint64_t timeout){
    return fnv_vk_backend_write_color(context,handle,in,bytes,timeout);
}
static void com_activation_trace(void *unused,const struct fnv_com_activation *r){
    (void)unused;
    report("COM activation clsid=%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X iid=%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X context=%08lX outer=%08lX hr=%08lX provider=%s\n",
        r->clsid[3],r->clsid[2],r->clsid[1],r->clsid[0],r->clsid[5],r->clsid[4],r->clsid[7],r->clsid[6],r->clsid[8],r->clsid[9],r->clsid[10],r->clsid[11],r->clsid[12],r->clsid[13],r->clsid[14],r->clsid[15],
        r->iid[3],r->iid[2],r->iid[1],r->iid[0],r->iid[5],r->iid[4],r->iid[7],r->iid[6],r->iid[8],r->iid[9],r->iid[10],r->iid[11],r->iid[12],r->iid[13],r->iid[14],r->iid[15],
        (unsigned long)r->context,(unsigned long)r->outer,(unsigned long)r->result,r->result?"unavailable":"native");
}
static void diagnostic(void *unused,const struct fnv_cpu_fault *f) {
    (void)unused;report("CPU fault kind=%u entry=%08lX address=%08lX detail=%08lX %s\n",
        (unsigned)f->kind,(unsigned long)f->entry,(unsigned long)f->address,(unsigned long)f->detail,f->text);
    report("CPU GPR eax=%08lX ecx=%08lX edx=%08lX ebx=%08lX esp=%08lX ebp=%08lX esi=%08lX edi=%08lX\n",
        (unsigned long)f->state.gpr[0],(unsigned long)f->state.gpr[1],(unsigned long)f->state.gpr[2],(unsigned long)f->state.gpr[3],
        (unsigned long)f->state.gpr[4],(unsigned long)f->state.gpr[5],(unsigned long)f->state.gpr[6],(unsigned long)f->state.gpr[7]);
}
static int identify(const char *path,const char *expected) {
    FILE *file=fopen(path,"rb");if(!file){report("Missing %s\n",path);return 0;}
    unsigned char *block=malloc(65536),digest[32];if(!block){fclose(file);return 0;}
    Sha256Context hash;sha256ContextCreate(&hash);size_t bytes;int ok=1;uint64_t total=0;
    while((bytes=fread(block,1,65536,file))){total+=bytes;if(total>128u<<20){ok=0;break;}sha256ContextUpdate(&hash,block,bytes);}
    if(ferror(file))ok=0;
    if(fclose(file))ok=0;
    free(block);if(!ok)return 0;
    sha256ContextGetHash(&hash,digest);char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);
    report("SHA256 %s %s bytes=%llu\n",path,hex,(unsigned long long)total);return !strcmp(hex,expected);
}
static uint32_t load32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
/* Each guest owner samples its own stack. Never reads another thread's TLS or
 * mutates guest state. Native boundaries only: not a CPU utilization profiler. */
static __attribute__((noinline)) void sample_native_record(uint32_t entry){
    struct fnv_cpu_state s;uint32_t low,high,id;
    if(fnv_cpu_get_state(&s)!=FNV_CPU_OK)return;
    fnv_windows_current_stack_bounds(&low,&high,&id);
    char words[145]={0};size_t used=0;uint32_t sp=s.gpr[4];
    if(sp>=low && sp<=high && high-sp>=64){
        const unsigned char *p=fnv_cpu_try_memory(sp,64);MemoryInfo info;uint32_t page;
        if(p && R_SUCCEEDED(svcQueryMemory(&info,&page,(uint64_t)(uintptr_t)p)) && (info.perm&Perm_R) &&
           info.addr<=(uint64_t)(uintptr_t)p && info.size>=64 && (uint64_t)(uintptr_t)p-info.addr<=info.size-64)
            for(unsigned i=0;i<16;i++)used+=(size_t)snprintf(words+used,sizeof(words)-used,"%08lX ",(unsigned long)load32(p+4*i));
    }
    const char *label="native-COM";
    if(entry>=0xfe000000u && entry<0xfe010000u){uint32_t index=(entry-0xfe000000u)/4;if(index<FNV_NATIVE_MAX_IMPORTS)label=import_labels[index];}
    char debug_name[128]="<busy>";(void)fnv_windows_try_debug_name(debug_name);
    u64 host_id=0,thread_ticks=0,thread_mask=0;s32 preferred=-1;
    Result thread_tick_rc=svcGetInfo(&thread_ticks,InfoType_ThreadTickCount,CUR_THREAD_HANDLE,UINT64_MAX);
    if(R_FAILED(thread_tick_rc))thread_tick_rc=svcGetInfo(&thread_ticks,InfoType_ThreadTickCountDeprecated,CUR_THREAD_HANDLE,UINT64_MAX);
    Result affinity_rc=svcGetThreadCoreMask(&preferred,&thread_mask,CUR_THREAD_HANDLE);(void)svcGetThreadId(&host_id,CUR_THREAD_HANDLE);
    report("NativeThread guest=%08lX host=%llu core=%ld preferred=%ld mask=%llX affinity_rc=%08lX ticks=%llu ticks_rc=%08lX\n",
        (unsigned long)id,(unsigned long long)host_id,(long)svcGetCurrentProcessorNumber(),(long)preferred,(unsigned long long)thread_mask,
        (unsigned long)affinity_rc,(unsigned long long)thread_ticks,(unsigned long)thread_tick_rc);
    report("NativeSample thread=%08lX name=%.80s entry=%08lX api=%.95s esp=%08lX ebp=%08lX calls=%llu stack=%s\n",
        (unsigned long)id,debug_name,(unsigned long)entry,label,(unsigned long)sp,(unsigned long)s.gpr[5],(unsigned long long)fnv_cpu_icall_count,words);
}
static void store32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static __attribute__((noinline)) void sample_native(uint32_t entry){
    static _Thread_local uint64_t last_tick;
    uint64_t tick=armGetSystemTick();
    if(last_tick && armTicksToNs(tick-last_tick)<10000000000ull)return;
    last_tick=tick;sample_native_record(entry);
}
static uint32_t graphics_query(void *context,uint32_t kind,const uint32_t *args,uint32_t count,void *out,uint32_t bytes) {
    if(kind==0 && count==6)return fnv_vk_backend_check_format(context,args[3],args[4],args[5]);
    if(kind==2 && count==2 && !args[0] && !(args[1]&~2u)) {
        uint32_t hr=fnv_vk_backend_adapter_identifier(context,out,bytes);
        /* NVK is the actual backend. Preserve real device/vendor identity;
         * use a truthful NVK driver name for the engine's prefix-based branch. */
        if(!hr && FNV_ENGINE_EXPERIMENTAL_CAPS && load32((unsigned char *)out+1064)==0x10de) {
            memset(out,0,512);memcpy(out,"nvk (FNV-nx Vulkan)",20);
        }
        return hr;
    }
    if(kind==1 && count==2 && !args[0] && args[1]==1) {
        uint32_t hr=fnv_vk_backend_device_caps(context,out,bytes);
        if(!hr && FNV_ENGINE_EXPERIMENTAL_CAPS) {
            unsigned char *p=out;store32(p+148,2);store32(p+152,2);p[30]|=1;
            const uint32_t limits[]={0,24,32,4,0,24,32,4,512,0,65535,65535,512,512};
            for(unsigned i=0;i<14;i++)store32(p+248+4*i,limits[i]);
            store32(p+196,0xfffe0300);store32(p+200,256);store32(p+204,0xffff0300);
        }
        return hr;
    }
    return 0x8876086au;
}
static int enumerate_display(void *unused,const char *parent,uint32_t index,uint32_t flags,struct fnv_display_device *out) {
    (void)unused;if(!out || flags>1)return -1;if(index)return 0;
    struct fnv_display_device d={0};
    if(!parent){strcpy(d.name,"\\\\.\\DISPLAY1");snprintf(d.description,sizeof(d.description),"%.127s",adapter+512);d.state_flags=5;}
    else if(!strcmp(parent,"\\\\.\\DISPLAY1")){strcpy(d.name,"\\\\.\\DISPLAY1\\Monitor0");strcpy(d.description,"Nintendo Switch display");d.state_flags=3;}
    else return 0;
    *out=d;return 1;
}
static int valid_window(void *context,uint32_t hwnd) {
    void *window=NULL;return fnv_windows_boot_ui_presentation(context,hwnd,&window)==FNV_CPU_OK && nwindowIsValid(window);
}
static int input_focus(void *context,uint32_t hwnd){return fnv_windows_boot_ui_input_focus(context,hwnd);}
static int movie_now(void *unused,uint64_t *ns){(void)unused;return fnv_host_now_ns(ns);}
static int movie_progress(void *unused){(void)unused;svcSleepThread(1000000);return audio && !fnv_horizon_audio_error(audio) && !atomic_load(&stop_requested);}
static uint32_t pool(uint32_t bytes){struct fnv_vm_result r=fnv_vm_allocate(vm,0,bytes,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);if(r.error)report("VM pool bytes=%lu error=%lu native=%08lX\n",(unsigned long)bytes,(unsigned long)r.error,(unsigned long)r.native_error);return r.error?0:r.value;}
static fnv_cpu_function profile_string_original,profile_integer_original;
static int guest_string(uint32_t address,const char *expected) {
    size_t length=strlen(expected);if((uint64_t)address+length>UINT32_MAX)return 0;
    for(size_t i=0;i<=length;i++){const unsigned char *p=fnv_cpu_try_memory(address+(uint32_t)i,1);if(!p || *p!=(unsigned char)expected[i])return 0;}return 1;
}
static void profile_string(void) {
    /* Adapter selection is virtual machine configuration, not a write to the
     * user's INI. All other profile requests retain their original provider. */
    if(guest_string(fnv_cpu_argument_u32(0),"Display") && guest_string(fnv_cpu_argument_u32(1),"sD3DDevice")) {
        uint32_t destination=fnv_cpu_argument_u32(3),capacity=fnv_cpu_argument_u32(4);
        const char *name=(const char *)adapter+512;size_t length=strlen(name);
        if(capacity){size_t copied=length<capacity-1?length:capacity-1;
            uint64_t end=(uint64_t)destination+copied+1;uint32_t vm_begin=plan->vm.base,vm_bytes=plan->vm.bytes;
            if((uint64_t)destination<(uint64_t)vm_begin+vm_bytes && end>vm_begin && !fnv_vm_access(vm,destination,(uint32_t)copied+1,1)){recomp_unimpl("Engine adapter preference output not writable",destination);return;}
            unsigned char *p=fnv_cpu_memory(destination,copied+1);memcpy(p,name,copied);p[copied]=0;fnv_windows_set_last_error(copied<length?234:0);fnv_cpu_return_u32((uint32_t)copied,24);}
        else fnv_cpu_return_u32(0,24);
        return;
    }
    profile_string_original();
}
static void profile_integer(void) {
    if(guest_string(fnv_cpu_argument_u32(0),"Display")) {
        uint32_t key=fnv_cpu_argument_u32(1),value=0;int matched=1;
        if(guest_string(key,"iSize W"))value=resolution.surface_width;
        else if(guest_string(key,"iSize H"))value=resolution.surface_height;
        else if(guest_string(key,"bFull Screen"))value=1;
        else matched=0;
        if(matched){fnv_cpu_return_u32(value,16);return;}
    }
    profile_integer_original();
}
struct exports {struct fnv_cpu_export *catalog,*extras;size_t count,extra;};
static int add_exports(struct exports *all,const struct fnv_cpu_export *e,size_t count,int kernel) {
    if(all->count+count>1024 || all->extra+(kernel?0:count)>FNV_NATIVE_MAX_IMPORTS)return 0;
    for(size_t i=0;i<count;i++) {
        struct fnv_cpu_export item=e[i];
        if(item.name && !strcasecmp(item.dll,"kernel32.dll") && !strcmp(item.name,"GetPrivateProfileStringA")){profile_string_original=item.function;item.function=profile_string;}
        if(item.name && !strcasecmp(item.dll,"kernel32.dll") && !strcmp(item.name,"GetPrivateProfileIntA")){profile_integer_original=item.function;item.function=profile_integer;}
        all->catalog[all->count++]=item;if(!kernel)all->extras[all->extra++]=item;
    }
    return 1;
}
static int import_matches(const struct fnv_cpu_export *e,const struct fnv_native_import *i){return !strcasecmp(e->dll,i->dll) && e->by_ordinal==i->by_ordinal && (i->by_ordinal?e->ordinal==i->ordinal:!strcmp(e->name,i->name));}
static atomic_uint_fast64_t trace_suppressed,trace_unique,trace_overflow;
static struct {uint64_t hash;size_t length;} trace_seen[256];
static void d3d9_trace_sink(const char *line) {
    uint64_t hash=UINT64_C(14695981039346656037);size_t length=0;
    for(;line[length];length++){hash^=(unsigned char)line[length];hash*=UINT64_C(1099511628211);}
    fnv_host_mutex_lock(&log_guard);
    for(unsigned i=0;i<256;i++){unsigned slot=((unsigned)hash+i)&255;
        if(!trace_seen[slot].length){trace_seen[slot].hash=hash;trace_seen[slot].length=length;
            atomic_fetch_add_explicit(&trace_unique,1,memory_order_relaxed);fputs(line,log_file);fflush(log_file);fnv_host_mutex_unlock(&log_guard);return;}
        if(trace_seen[slot].hash==hash && trace_seen[slot].length==length){
            atomic_fetch_add_explicit(&trace_suppressed,1,memory_order_relaxed);fnv_host_mutex_unlock(&log_guard);return;}
    }
    atomic_fetch_add_explicit(&trace_overflow,1,memory_order_relaxed);fnv_host_mutex_unlock(&log_guard);
}
static uint32_t guest32(uint32_t at) {const unsigned char *p=fnv_cpu_try_memory(at,4);return p?load32(p):0;}
static uint32_t guest16(uint32_t at) {const unsigned char *p=fnv_cpu_try_memory(at,2);return p?(uint32_t)p[0]|(uint32_t)p[1]<<8:0;}
static void guest_text(const char *label,uint32_t at) {
    char text[257];unsigned i=0;
    while(i<256){const unsigned char *p=at?fnv_cpu_try_memory(at+i,1):NULL;if(!p||!*p)break;char c=(char)*p;text[i++]=c=='\n'||c=='\r'||c=='\t'?' ':c;}
    text[i]=0;report("%s=[%s] bytes=%u\n",label,text,i);
}
/* Diagnostic MessageBoxA trap: record the guest state around the call, then
 * fault exactly like an unresolved import. Never returns success. */
static void message_box_a(void) {
    uint32_t owner=fnv_cpu_argument_u32(0),text=fnv_cpu_argument_u32(1),caption=fnv_cpu_argument_u32(2),type=fnv_cpu_argument_u32(3);
    struct fnv_cpu_state state;uint32_t caller=0;
    if(fnv_cpu_get_state(&state)==FNV_CPU_OK){const unsigned char *p=fnv_cpu_try_memory(state.gpr[4],4);if(p)caller=load32(p);}
    report("MessageBoxA owner=%08lX text=%08lX caption=%08lX type=%08lX caller=%08lX\n",(unsigned long)owner,(unsigned long)text,(unsigned long)caption,(unsigned long)type,(unsigned long)caller);
    guest_text("MessageBoxA text",text);guest_text("MessageBoxA caption",caption);
    report("Guest globals 11C6FBA=%04lX 11C6FBC=%08lX 11C6FC0=%08lX 11C6FC4=%08lX 11C6FC8=%08lX 11C70CC=%08lX 11C73B4=%08lX 126F0D8=%08lX 126F0DC=%08lX 126F0E0=%08lX\n",
        (unsigned long)guest16(0x11C6FBA),(unsigned long)guest32(0x11C6FBC),(unsigned long)guest32(0x11C6FC0),(unsigned long)guest32(0x11C6FC4),
        (unsigned long)guest32(0x11C6FC8),(unsigned long)guest32(0x11C70CC),(unsigned long)guest32(0x11C73B4),
        (unsigned long)guest32(0x126F0D8),(unsigned long)guest32(0x126F0DC),(unsigned long)guest32(0x126F0E0));
    struct fnv_d3d9_com_stats stats={0};
    if(!fnv_d3d9_com_stats(graphics_com,&stats))report("D3D9 stats refs=%lu created=%lu devrefs=%lu attempts=%lu ok=%lu fail=%lu last=%08lX factory=%08lX device=%08lX\n",
        (unsigned long)stats.factory_refs,(unsigned long)stats.factory_created,(unsigned long)stats.device_references,
        (unsigned long)stats.create_attempts,(unsigned long)stats.create_successes,(unsigned long)stats.create_failures,(unsigned long)stats.last_create_result,
        (unsigned long)fnv_d3d9_com_factory(graphics_com),(unsigned long)fnv_d3d9_com_device(graphics_com));
    recomp_unimpl("MessageBoxA diagnostic trap",caller);
}
static int initialize(void) {
    report("FNV PC engine %s: captured-code experiment, cold_boot=0 experimental_caps=%d volume=1 cfg_corrected_units=%u tail_optimized_units=%u\n",FNV_ENGINE_HW_TAG,FNV_ENGINE_EXPERIMENTAL_CAPS,FNV_ENGINE_CFG_CORRECTED_UNITS,FNV_ENGINE_TAIL_OPTIMIZED_UNITS);
    report("SoftFloat state=%s\n",FNV_ENGINE_X87_THREAD_LOCAL_STATE?"thread-local":"shared-mutex");
    REQUIRE(appletGetAppletType()==AppletType_Application || appletGetAppletType()==AppletType_SystemApplication,"application memory mode (title takeover)");
    REQUIRE(identify(ROOT "/FalloutNV.exe",FNV_ENGINE_EXE_SHA256),"original supported EXE identity");
    REQUIRE(identify("romfs:/captured-text-data.bin",FNV_ENGINE_TEXT_SHA256),"captured text identity");
    REQUIRE(identify("romfs:/catalog.sdp",FNV_ENGINE_SDP_SHA256),"private shader catalog identity");
    plan=calloc(1,sizeof(*plan));REQUIRE(plan,"process blueprint allocation");
    unsigned char *arena=memalign(4096,808u<<20);REQUIRE(arena,"808 MiB guest backing");
    fnv_engine_crash_guest(arena,FNV_ENGINE_BASE,808u<<20);
    fnv_vm_horizon_permissions(&permissions);
    REQUIRE(fnv_engine_process_plan_create(FNV_ENGINE_BASE,FNV_ENGINE_IMAGE_BYTES,arena,808u<<20,&permissions,plan),"nonoverlapping engine memory plan");
    REQUIRE(fnv_native_load_image(ROOT "/FalloutNV.exe",arena,plan->layout.arena_bytes,&image)==FNV_NATIVE_OK && image.base==FNV_ENGINE_BASE && image.size==FNV_ENGINE_IMAGE_BYTES,"PE data load");
    for(unsigned i=FNV_ENGINE_PEB;i<FNV_ENGINE_VM;i++)memset(arena+plan->layout.regions[i].offset,0,plan->layout.regions[i].bytes);
    unsigned char *text=fnv_native_guest_range(&image,image.base+FNV_ENGINE_TEXT_RVA,FNV_ENGINE_TEXT_BYTES);REQUIRE(text,"capture destination");
    FILE *blob=fopen("romfs:/captured-text-data.bin","rb");REQUIRE(blob,"capture open");
    int copied=fread(text,1,FNV_ENGINE_TEXT_BYTES,blob)==FNV_ENGINE_TEXT_BYTES && fgetc(blob)==EOF && !ferror(blob);
    int closed=fclose(blob);REQUIRE(copied && !closed,"captured code/data overlay");
    struct fnv_vm_result vr=fnv_vm_create(&plan->vm,&vm);report("VM create error=%lu native=%08lX\n",(unsigned long)vr.error,(unsigned long)vr.native_error);REQUIRE(!vr.error,"Horizon VM permissions");
    plan->process.memory=vm;plan->process.cpu.diagnostic=diagnostic;
    fnv_windows_com_activation_trace(NULL,com_activation_trace);
    REQUIRE(fnv_windows_create(&plan->process,&process)==FNV_CPU_OK,"Windows process");
    REQUIRE(fnv_display_resolution_horizon(nwindowGetDefault(),&resolution),"Horizon display facts");
    report("Display mode=%lu output=%lux%lu surface=%lux%lu\n",(unsigned long)resolution.mode,(unsigned long)resolution.output_width,(unsigned long)resolution.output_height,(unsigned long)resolution.surface_width,(unsigned long)resolution.surface_height);
    struct fnv_vk_config gc={resolution.surface_width,resolution.surface_height,4096,2,128,256u<<20,0};VkResult failure=0;
    uint32_t hr=fnv_vk_backend_create_horizon(vkGetInstanceProcAddr,&gc,nwindowGetDefault(),1,&graphics,&failure);
    report("Vulkan create hr=%08lX native=%ld\n",(unsigned long)hr,(long)failure);REQUIRE(!hr,"NVK window/swapchain");
    REQUIRE(!fnv_vk_backend_adapter_identifier(graphics,adapter,sizeof(adapter)),"actual Vulkan adapter");
    report("Adapter %.511s / %.511s vendor=%04lX device=%04lX\n",adapter,adapter+512,(unsigned long)load32(adapter+1064),(unsigned long)load32(adapter+1068));
    REQUIRE(!fnv_vk_backend_presentation_format(graphics,22) && !fnv_vk_backend_draw_wait(graphics,10000000000ull),"graphics presentation configuration");
    REQUIRE(!fnv_vk_backend_uniform_reuse(graphics,1),"retired-frame uniform reuse");
    REQUIRE(!fnv_vk_backend_layout_reuse(graphics,1),"immutable descriptor layout reuse");
    report("GPU timing setup hr=%08lX stride=16 optional=1\n",(unsigned long)fnv_vk_backend_gpu_timing(graphics,16));
    report("Renderer reuse: resource index, retired-frame mapped uniforms, layouts=16; audio silent-voice fast path\n");
    report("CPU hot leaf inlining: units=%u direct_sites=%u guest stack/register writes retained\n",FNV_ENGINE_INLINE_UNITS,FNV_ENGINE_INLINE_SITES);
    struct fnv_display_backend db={NULL,enumerate_display};struct fnv_windows_display *display;
    REQUIRE(fnv_windows_display_create(process,&db,&display)==FNV_CPU_OK,"display provider");
    REQUIRE(fnv_windows_boot_ui_create(process,&image,&boot_ui)==FNV_CPU_OK,"window provider");
    struct fnv_user32_metrics metrics={&resolution,fnv_display_resolution_metrics};
    REQUIRE(fnv_windows_user32_create_with_metrics(process,&metrics,&user32)==FNV_CPU_OK,"USER32 metrics");
    struct fnv_boot_window_backend wb;REQUIRE(fnv_boot_horizon_backend(&window_binding,nwindowGetDefault(),&wb) && fnv_windows_boot_ui_set_backend(boot_ui,user32,&wb)==FNV_CPU_OK,"NWindow lease");
    uint32_t input_pool=pool(65536),mmio_pool=pool(0x11000),vorbis_pool=pool(0x11000*16),sound_pool=pool(16u<<20),movie_pool=pool(65536);
    REQUIRE(input_pool && mmio_pool && vorbis_pool && sound_pool && movie_pool,"media/input pools");
    struct fnv_dinput8_config di={0};di.instance=image.base;di.objects_address=input_pool;di.objects_bytes=65536;di.keyboard=di.mouse=1;di.memory=vm;di.context=boot_ui;di.valid_window=valid_window;di.has_input_focus=input_focus;
    REQUIRE(fnv_dinput8_create(process,&di,&dinput)==FNV_CPU_OK,"DirectInput provider");
    REQUIRE(fnv_horizon_pad_open(process,1,&controller)==FNV_CPU_OK,"Switch XInput controller");
    struct fnv_mmio_config mc={mmio_pool,0x11000,vm};struct fnv_windows_mmio *mmio;
    REQUIRE(fnv_windows_mmio_create(process,&mc,&mmio)==FNV_CPU_OK,"MMIO provider");
    struct fnv_vorbis_config vc={vorbis_pool,0x11000*16,16,vm};struct fnv_windows_vorbis *vorbis;
    REQUIRE(fnv_windows_vorbis_create(process,&vc,&vorbis)==FNV_CPU_OK,"Vorbis provider");
    struct fnv_pcm_queue *queue;REQUIRE(fnv_pcm_create(2048,&queue)==FNV_PCM_OK,"PCM queue");
    struct fnv_dsound8_config sc={sound_pool,16u<<20,1,vm,queue,boot_ui,valid_window};
    REQUIRE(fnv_dsound8_create(process,&sc,&sound)==FNV_CPU_OK,"DirectSound mixer");
    uint32_t graph_pool=pool(0x90000);struct fnv_directshow *directshow;
    struct fnv_directshow_config dc={graph_pool,0x90000,vm,sound};
    REQUIRE(graph_pool && fnv_directshow_create(process,&dc,&directshow)==FNV_CPU_OK,"DirectShow pull-MP3 graph");
    report("DirectShow pull-MP3 provider installed: guest IAsyncReader, independent music rings, owner-thread callbacks\n");
    REQUIRE(!fnv_horizon_audio_open_source(queue,fnv_dsound8_produce,sound,&audio),"native audout worker");
    REQUIRE(R_SUCCEEDED(audoutSetAudioOutVolume(1.0f)),"audio output volume");
    struct fnv_bink_movie_config bc={movie_pool,65536,sound,(void *)ROOT,fnv_engine_movie_open,fnv_engine_movie_close,movie_now,movie_progress};struct fnv_bink_movie *movie;
    REQUIRE(fnv_bink_movie_create(process,&bc,&movie)==FNV_CPU_OK,"Bink decoder/controller");
    struct fnv_bink_sound_config bs={movie,fnv_bink_movie_sound_factory};struct fnv_bink_sound *bink_sound;
    REQUIRE(fnv_bink_sound_create(process,&bs,&bink_sound)==FNV_CPU_OK && fnv_bink_movie_bind_sound(movie,bink_sound)==FNV_CPU_OK,"Bink sound bridge");
    struct fnv_steam_callbacks *steam;struct fnv_windows_system *system;
    u64 startup_core_mask=0;uint32_t logical_processors=3;
    Result startup_core_result=svcGetInfo(&startup_core_mask,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
    if(R_SUCCEEDED(startup_core_result) && (startup_core_mask&15)){
        logical_processors=0;for(unsigned core=0;core<4;core++)logical_processors+=(startup_core_mask>>core)&1u;
    }
    report("CPU guest_processors=%u allowed_mask=%llX query_rc=%08lX\n",logical_processors,(unsigned long long)startup_core_mask,(unsigned long)startup_core_result);
    REQUIRE(fnv_steam_callbacks_create(process,&steam)==FNV_CPU_OK && fnv_windows_system_create(process,logical_processors,&system)==FNV_CPU_OK,"system providers");
    struct fnv_d3d9_sdp *sdp;REQUIRE(fnv_sdp_load("romfs:/catalog.sdp",16u<<20,&sdp)==FNV_SDP_OK,"original shader catalog");
    size_t catalog_count;const struct fnv_d3d9_shader_code *codes=fnv_sdp_entries(sdp,&catalog_count);REQUIRE(catalog_count==FNV_ENGINE_CATALOG_COUNT,"shader catalog inventory");
    for(size_t i=0;i<catalog_count;i++){const struct fnv_d3d9_shader_code *code=codes+i;uint32_t version=load32(code->bytes);REQUIRE(!fnv_vk_backend_declare_shader(graphics,(version>>16)==0xffff?FNV_D3D9_PIXEL:FNV_D3D9_VERTEX,code->bytes,code->size),"shader declaration");}
    configure_fixed_screen(graphics);size_t failed=SIZE_MAX;
    hr=fnv_vk_prepare_programs(graphics,fnv_startup_programs,FNV_STARTUP_PROGRAM_COUNT,codes,catalog_count,&failed);
    report("Shader programs count=%u hr=%08lX failed_index=%llu experimental_caps=%d generic_conformance=0\n",FNV_STARTUP_PROGRAM_COUNT,(unsigned long)hr,(unsigned long long)failed,FNV_ENGINE_EXPERIMENTAL_CAPS);REQUIRE(!hr,"prepared shader programs");
    uint32_t graphics_objects=pool(65536),graphics_buffers=pool(128u<<20);REQUIRE(graphics_objects && graphics_buffers,"D3D9 COM pools");
    struct fnv_d3d9_factory_profile factory={gc.width,gc.height,22,0,60,graphics,graphics_query};
    struct fnv_d3d9_com_config cc={0};cc.objects_address=graphics_objects;cc.objects_bytes=65536;cc.resource_capacity=4096;cc.vertex_floats=256;cc.pixel_floats=224;cc.memory=vm;cc.buffer_address=graphics_buffers;cc.buffer_bytes=128u<<20;cc.factory=&factory;cc.catalog=codes;cc.catalog_count=catalog_count;cc.presentation_timeout_ns=10000000000ull;fnv_vk_backend_callbacks(graphics,&cc.backend);
    fnv_d3d9_trace_hook=d3d9_trace_sink;report("D3D9 trace sink enabled unique_cap=256 repeated_lines=counted\n");
    REQUIRE(fnv_d3d9_com_create(fnv_windows_runtime(process),&cc,&graphics_com)==FNV_CPU_OK,"D3D9 COM factory");
    {struct fnv_d3d9_surface_io io={graphics,surface_read,surface_write};fnv_d3d9_com_surface_io(graphics_com,&io);report("D3D9 lockable backbuffer transfer provider installed\n");}
    fnv_sdp_destroy(sdp);
    struct exports all={calloc(1024,sizeof(struct fnv_cpu_export)),calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(struct fnv_cpu_export)),0,0};REQUIRE(all.catalog && all.extras,"import catalogs");
    size_t n;const struct fnv_cpu_export *e=fnv_windows_exports(&n);REQUIRE(add_exports(&all,e,n,1),"kernel exports");
#define ADD(fn) do{e=fn(&n);REQUIRE(add_exports(&all,e,n,0),#fn);}while(0)
    ADD(fnv_windows_modules_exports);ADD(fnv_windows_memory_exports);ADD(fnv_dinput8_exports);ADD(fnv_xinput_exports);
    ADD(fnv_windows_debug_exports);ADD(fnv_windows_d3dx_exports);ADD(fnv_windows_com_exports);ADD(fnv_windows_multimedia_exports);
    ADD(fnv_windows_vorbis_exports);ADD(fnv_windows_mmio_exports);ADD(fnv_windows_display_exports);ADD(fnv_dsound8_exports);
    ADD(fnv_bink_sound_exports);ADD(fnv_bink_movie_exports);ADD(fnv_windows_filetime_exports);ADD(fnv_windows_strings_exports);
    ADD(fnv_windows_boot_ui_exports);ADD(fnv_windows_user32_exports);ADD(fnv_windows_system_exports);ADD(fnv_windows_profile_exports);
    ADD(fnv_windows_shell_exports);ADD(fnv_steam_callbacks_exports);ADD(fnv_windows_registry_exports);
#undef ADD
    e=fnv_d3d9_com_exports(graphics_com,&n);REQUIRE(add_exports(&all,e,n,0),"D3D9 exports");
    struct fnv_windows_modules *modules;REQUIRE(fnv_windows_modules_create(process,&image,"FalloutNV.exe",all.catalog,all.count,&modules)==FNV_CPU_OK,"native DLL catalog");
    struct fnv_native_import *imports=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*imports)),*scratch=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*scratch));size_t count,bound;
    REQUIRE(imports && scratch && fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&count)==FNV_NATIVE_OK && count==FNV_ENGINE_IMPORT_COUNT,"original IAT inventory");
    size_t supported=0;for(size_t i=0;i<count;i++){int found=0;for(size_t j=0;j<all.count;j++)if(import_matches(all.catalog+j,imports+i))found=1;if(found){supported++;continue;}REQUIRE(all.extra<FNV_NATIVE_MAX_IMPORTS,"trap catalog capacity");struct fnv_cpu_export trap={imports[i].dll,imports[i].by_ordinal?NULL:imports[i].name,imports[i].ordinal,imports[i].by_ordinal,fnv_engine_missing_imports[i]};if(!imports[i].by_ordinal && !strcasecmp(imports[i].dll,"user32.dll") && !strcmp(imports[i].name,"MessageBoxA"))trap.function=message_box_a;all.extras[all.extra++]=trap;}
    REQUIRE(fnv_windows_bind_imports(process,&image,scratch,FNV_NATIVE_MAX_IMPORTS,all.extras,all.extra,&bound)==FNV_NATIVE_OK && bound==count,"IAT and static TLS binding");
    for(size_t i=0;i<count;i++){
        if(imports[i].by_ordinal)snprintf(import_labels[i],sizeof(import_labels[i]),"%.64s!#%u",imports[i].dll,imports[i].ordinal);
        else snprintf(import_labels[i],sizeof(import_labels[i]),"%.40s!%.53s",imports[i].dll,imports[i].name);
    }
    fnv_cpu_set_native_observer(sample_native);report("Loading diagnostics: native boundary samples and IO/wait counters enabled\n");
    report("Ready entry=%08lX imports=%u/%u trapped=%u programs=%u root=%s\n",(unsigned long)FNV_ENGINE_ENTRY,(unsigned)supported,(unsigned)count,(unsigned)(count-supported),FNV_STARTUP_PROGRAM_COUNT,ROOT);
    free(imports);free(scratch);free(all.catalog);free(all.extras);return 1;
}
static void guest_main(void *unused) {
    (void)unused;struct fnv_windows_thread_config *t=&plan->main_thread;
    report("Guest main: entered native thread\n");fnv_engine_crash_phase(4);
    fnv_cpu_state_init(&t->state,t->stack_high-64,t->teb_address);
    execution_result=fnv_windows_thread_attach(process,t);
    report("Guest main: attach=%u\n",execution_result);
    if(execution_result==FNV_CPU_OK) {
        store32(plan->process.cpu.memory+t->state.gpr[4]-image.base,0);
        report("Guest main: execute=%08lX\n",(unsigned long)FNV_ENGINE_ENTRY);fnv_engine_crash_phase(5);
        execution_result=fnv_cpu_execute(FNV_ENGINE_ENTRY,&execution_fault);fnv_engine_crash_phase(6);
        enum fnv_cpu_result detached=fnv_windows_thread_detach();report("Main guest return=%u detach=%u\n",execution_result,detached);
    }else report("Main attach failed=%u\n",execution_result);
    atomic_store(&done,1);
}
static int poll_input(struct fnv_di_horizon_poll *poll) {
    HidKeyboardState keyboard={0};HidMouseState mouse[17];struct fnv_di_snapshot snap={0};
    fnv_engine_watchdog_stage(FNV_MON_KEYBOARD);
    size_t nk=hidGetKeyboardStates(&keyboard,1);fnv_engine_watchdog_stage(FNV_MON_MOUSE);size_t nm=hidGetMouseStates(mouse,17);
    if(nk && fnv_di_usb_keyboard(keyboard.keys,keyboard.modifiers,&snap))return 0;
    struct fnv_di_mouse_sample samples[17];for(size_t i=0;i<nm;i++)samples[i]=(struct fnv_di_mouse_sample){mouse[i].sampling_number,mouse[i].delta_x,mouse[i].delta_y,mouse[i].wheel_delta_y,mouse[i].buttons};
    if(!fnv_di_collect_mouse(samples,(unsigned)nm,poll,&snap))return 0;
    int focused=appletGetFocusState()==AppletFocusState_InFocus;
    if(!focused)memset(&snap,0,sizeof(snap));
    fnv_engine_watchdog_stage(FNV_MON_DINPUT);if(fnv_dinput8_update(dinput,&snap)!=FNV_CPU_OK)return 0;
    fnv_engine_watchdog_stage(FNV_MON_USER32);if(fnv_user32_update_di_snapshot(user32,&snap,focused)!=FNV_CPU_OK)return 0;
    static int last_focus=-1;
    if(last_focus!=focused){fnv_engine_watchdog_stage(FNV_MON_SOUND);if(fnv_dsound8_set_focus(sound,focused)!=FNV_CPU_OK)return 0;last_focus=focused;}
    fnv_engine_watchdog_stage(FNV_MON_CONTROLLER);return fnv_horizon_pad_poll(controller)==FNV_CPU_OK;
}
static void request_stop(const char *reason) {
    if(atomic_exchange(&stop_requested,1))return;
    if(fnv_cpu_is_faulted(fnv_windows_runtime(process)))return;
    struct fnv_cpu_fault fault={0};fault.kind=FNV_CPU_UNIMPLEMENTED;snprintf(fault.text,sizeof(fault.text),"Diagnostic stop: %.70s",reason);fnv_cpu_invalidate(fnv_windows_runtime(process),&fault);
}
int main(void) {
    consoleInit(NULL);printf("FNV PC engine experiment\nPrivate captured-code startup; audio enabled.\nHold L + R + Minus to stop.\nSee " ROOT "/fnv-pc-engine-" FNV_ENGINE_HW_TAG ".log\n");consoleUpdate(NULL);
    fnv_host_mutex_init(&log_guard);log_file=fopen(ROOT "/fnv-pc-engine-" FNV_ENGINE_HW_TAG ".log","w");
    if(!log_file){printf("Cannot open log in " ROOT "\n");consoleUpdate(NULL);svcSleepThread(3000000000ll);consoleExit(NULL);return 1;}
    uint32_t crash_sink=fnv_engine_crash_prepare(FNV_ENGINE_HW_TAG);report("Native crash sink=%08lX\n",(unsigned long)crash_sink);fnv_engine_crash_phase(1);
    if(crash_sink){report("Cannot prepare native crash record; guest execution not started\n");consoleExit(NULL);svcExitProcess();}
    Result rom=romfsInit();if(R_FAILED(rom)){report("ROMFS error=%08lX\n",(unsigned long)rom);fclose(log_file);consoleExit(NULL);return 1;}
    /* Release console framebuffer before any Vulkan swapchain ownership. */
    consoleExit(NULL);hidInitializeKeyboard();hidInitializeMouse();
    if(!initialize()){report("Initialization failed; no guest code executed\n");fflush(log_file);svcSleepThread(2000000000ll);svcExitProcess();}
    PadState exit_pad;padInitializeDefault(&exit_pad);fnv_host_thread thread;
    if(!fnv_host_thread_create(&thread,guest_main,NULL,4u<<20)){report("Cannot create engine host thread\n");svcExitProcess();}
    if(!fnv_engine_watchdog_start(threadGetCurHandle(),thread.thread.handle)){report("Cannot start independent watchdog\n");svcExitProcess();}
    struct fnv_di_horizon_poll poll={0};uint64_t start=armGetSystemTick(),last=start,stopping=0,seen_buttons=0;unsigned exit_hold=0;
    while(!atomic_load(&done)) {
        fnv_engine_watchdog_stage(FNV_MON_APPLET);if(!appletMainLoop())request_stop("applet exit");
        fnv_engine_watchdog_stage(FNV_MON_PAD);padUpdate(&exit_pad);u64 buttons=padGetButtons(&exit_pad),combo=HidNpadButton_L|HidNpadButton_R|HidNpadButton_Minus;
        seen_buttons|=padGetButtonsDown(&exit_pad);
        if((buttons&combo)==combo){if(++exit_hold>=125)request_stop("user L+R+Minus");}else exit_hold=0;
        if(!poll_input(&poll))request_stop("input backend failure");
        uint32_t audio_error=fnv_horizon_audio_error(audio);if(audio_error){report("Audio error=%lu\n",(unsigned long)audio_error);request_stop("audout failure");}
        /* Prepared NWindow geometry is immutable during this diagnostic.
         * Avoid VI IPC/NWindow locking every 8 ms; restart on dock-mode change. */
        fnv_engine_watchdog_stage(FNV_MON_DISPLAY);
        if((uint32_t)appletGetOperationMode()!=resolution.mode)request_stop("display mode changed; restart diagnostic");
        uint64_t now=armGetSystemTick();
        fnv_engine_watchdog_stage(FNV_MON_STOP);
        if(atomic_load(&stop_requested)) {
            if(!stopping)stopping=now;
            if(armTicksToNs(now-stopping)>=5000000000ull){report("Guest did not unwind within 5s; OS process exit, backing not freed\n");svcExitProcess();}
        }
        if(armTicksToNs(now-last)>=10000000000ull) {
            fnv_engine_watchdog_stage(FNV_MON_STATS);
            uint64_t elapsed=armTicksToNs(now-start)/1000000;
            struct fnv_vk_progress progress;fnv_vk_backend_progress(graphics,&progress);
            const char *phases[]={"idle","draw","geometry","texture","allocate","shader","pipeline","fence","submit","acquire","present"};
            uint64_t ns=armTicksToNs(now),age=progress.since_ns && ns>=progress.since_ns?(ns-progress.since_ns)/1000000:0;
            report("Heartbeat elapsed_ms=%llu render_phase=%s phase_age_ms=%llu fence_calls=%llu fence_ms=%llu submit_calls=%llu submit_ms=%llu\n",(unsigned long long)elapsed,progress.phase<sizeof(phases)/sizeof(*phases)?phases[progress.phase]:"invalid",(unsigned long long)age,(unsigned long long)progress.wait_calls,(unsigned long long)(progress.wait_ns/1000000),(unsigned long long)progress.submit_calls,(unsigned long long)(progress.submit_ns/1000000));
            struct fnv_vk_stats stats;struct fnv_vk_pipeline_metrics pipelines;
            uint32_t snapshot=fnv_vk_backend_try_snapshot(graphics,&stats,&pipelines);
            if(!snapshot){
                report("Progress elapsed_ms=%llu draws=%llu submissions=%llu presents=%llu gpu_alloc=%llu\n",(unsigned long long)elapsed,(unsigned long long)stats.draws,(unsigned long long)stats.submissions,(unsigned long long)stats.presentation_requests,(unsigned long long)stats.allocated_bytes);
                report("Pipelines calls=%llu host_ms=%llu max_host_ms=%llu cache=device\n",(unsigned long long)pipelines.calls,(unsigned long long)(pipelines.host_ns/1000000),(unsigned long long)(pipelines.max_host_ns/1000000));
                report("Reuse lookup_calls=%llu hits=%llu scan_steps=%llu uniform_allocs=%llu uniform_reuses=%llu\n",(unsigned long long)stats.lookup_calls,(unsigned long long)stats.lookup_hits,(unsigned long long)stats.lookup_scan_steps,(unsigned long long)stats.uniform_allocations,(unsigned long long)stats.uniform_reuses);
                report("Draw attempts=%llu invalid=%llu unsupported=%llu busy=%llu\n",(unsigned long long)stats.draw_attempts,(unsigned long long)stats.draw_invalid,(unsigned long long)stats.draw_unsupported,(unsigned long long)stats.draw_busy);
                report("Layouts creates=%llu hits=%llu\n",(unsigned long long)stats.layout_creates,(unsigned long long)stats.layout_hits);
                report("ProgramLookup hits=%llu scans=%llu\n",(unsigned long long)stats.program_lookup_hits,(unsigned long long)stats.program_lookup_scans);
                report("GPUTime samples=%llu sampled_ns=%llu draw_ns=%llu other_ns=%llu failures=%llu stride=%u timestamp_bits=%u\n",
                    (unsigned long long)stats.gpu_samples,(unsigned long long)stats.gpu_sample_ns,(unsigned long long)stats.gpu_draw_ns,
                    (unsigned long long)stats.gpu_other_ns,(unsigned long long)stats.gpu_query_failures,stats.gpu_sample_stride,stats.gpu_timestamp_bits);
                static uint64_t frame_previous_ms,frame_previous_count,frame_previous_draws;
                if(frame_previous_ms && elapsed>frame_previous_ms && stats.presentation_requests>=frame_previous_count && stats.draws>=frame_previous_draws){
                    uint64_t window=elapsed-frame_previous_ms,frames=stats.presentation_requests-frame_previous_count;
                    uint64_t fps_milli=frames*1000000/window,draws=stats.draws-frame_previous_draws,dpf=frames?draws*1000/frames:0;
                    report("FrameRate window_ms=%llu fps=%llu.%03llu frames=%llu total_frames=%llu mean_interval_us=%llu draws=%llu draws_per_frame=%llu.%03llu interval_valid=%u\n",
                        (unsigned long long)window,(unsigned long long)(fps_milli/1000),(unsigned long long)(fps_milli%1000),
                        (unsigned long long)frames,(unsigned long long)stats.presentation_requests,(unsigned long long)(frames?window*1000/frames:0),(unsigned long long)draws,
                        (unsigned long long)(dpf/1000),(unsigned long long)(dpf%1000),frames?1u:0u);
                }
                frame_previous_ms=elapsed;frame_previous_count=stats.presentation_requests;frame_previous_draws=stats.draws;
                static const char *draw_reasons[16]={"state","inputs","targets","viewport","program","indices","geometry","attributes","texture","sampler","depth_blend","queue","uniforms","descriptors","pipeline","submit"};
                char reason_line[512]={0};size_t reason_used=0;
                for(unsigned reason=0;reason<16;reason++){
                    int added=snprintf(reason_line+reason_used,sizeof(reason_line)-reason_used," %s=%llu",draw_reasons[reason],(unsigned long long)stats.draw_reasons[reason]);
                    if(added<0 || (size_t)added>=sizeof(reason_line)-reason_used)break;
                    reason_used+=(size_t)added;
                }
                report("DrawReject%s\n",reason_line);
                report("MissingPairs count=%u overflow=%llu\n",stats.missing_pairs.count,(unsigned long long)stats.missing_pairs.overflow);
                for(uint32_t pair=0;pair<stats.missing_pairs.count;pair++){
                    const struct fnv_shader_pair_record *r=stats.missing_pairs.rows+pair;
                    report("MissingPair vs=%016llx/%u ps=%016llx/%u fixed=%u reason=%u hits=%llu\n",
                        (unsigned long long)r->vertex_hash,r->vertex_bytes,(unsigned long long)r->pixel_hash,r->pixel_bytes,
                        r->fixed_bytes,r->reason,(unsigned long long)r->hits);
                    char attributes[160]={0};size_t used=0;
                    for(uint32_t a=0;a<r->attribute_count && a<16;a++){
                        int n=snprintf(attributes+used,sizeof(attributes)-used,"%s%06x",a?",":"",r->attributes[a]);
                        if(n<0 || (size_t)n>=sizeof(attributes)-used)break;
                        used+=(size_t)n;
                    }
                    report("MissingPairAttributes vs=%016llx/%u ps=%016llx/%u fixed=%u reason=%u state=%u count=%u attrs=%s\n",
                        (unsigned long long)r->vertex_hash,r->vertex_bytes,(unsigned long long)r->pixel_hash,r->pixel_bytes,
                        r->fixed_bytes,r->reason,r->attribute_state,r->attribute_count,attributes);
                }
                char phase_line[512]={0};size_t phase_used=0;
                for(unsigned phase=0;phase<11;phase++){
                    int added=snprintf(phase_line+phase_used,sizeof(phase_line)-phase_used," %s_ms=%llu",phases[phase],(unsigned long long)(stats.host_phase_ns[phase]/1000000));
                    if(added<0 || (size_t)added>=sizeof(phase_line)-phase_used)break;
                    phase_used+=(size_t)added;
                }
                report("HostPhases%s\n",phase_line);
            }else report("Renderer snapshot busy hr=%08lX\n",(unsigned long)snapshot);
            struct fnv_dsound_metrics sound_metrics;
            if(fnv_dsound8_try_metrics(sound,&sound_metrics)==FNV_PCM_OK)report("Audio pumps=%llu frames=%llu full=%llu lock_ms=%llu mix_ms=%llu max_us=%llu late=%llu silent_voice_frames=%llu\n",(unsigned long long)sound_metrics.pumps,(unsigned long long)sound_metrics.frames,(unsigned long long)sound_metrics.full_returns,(unsigned long long)(sound_metrics.lock_wait_ns/1000000),(unsigned long long)(sound_metrics.mix_ns/1000000),(unsigned long long)(sound_metrics.max_pump_ns/1000),(unsigned long long)sound_metrics.late_pumps,(unsigned long long)sound_metrics.silent_voice_frames);
            else report("Audio metrics busy\n");
            if(fnv_dsound8_try_metrics(sound,&sound_metrics)==FNV_PCM_OK)report("DSBuffers slots=%u live=%u playing=%u spatial=%u pcm_upload_bytes=%llu\n",
                sound_metrics.allocated_slots,sound_metrics.live_buffers,sound_metrics.playing_buffers,sound_metrics.spatial_buffers,(unsigned long long)sound_metrics.pcm_upload_bytes);
            report("Trace unique=%llu suppressed=%llu overflow=%llu\n",(unsigned long long)atomic_load(&trace_unique),(unsigned long long)atomic_load(&trace_suppressed),(unsigned long long)atomic_load(&trace_overflow));
            struct fnv_vorbis_metrics vorbis_metrics;if(fnv_windows_vorbis_metrics(&vorbis_metrics))report("Vorbis opens=%llu failures=%llu reads=%llu decoded_bytes=%llu last_error=%ld\n",
                (unsigned long long)vorbis_metrics.opens,(unsigned long long)vorbis_metrics.open_failures,(unsigned long long)vorbis_metrics.reads,(unsigned long long)vorbis_metrics.decoded_bytes,(long)vorbis_metrics.last_error);
            struct fnv_bink_decode_metrics bink_decode;struct fnv_bink_movie_metrics bink_movie;
            fnv_bink_decoder_metrics(&bink_decode);fnv_bink_movie_metrics(&bink_movie);
            report("Bink video_frames=%llu audio_frames=%llu next_ns=%llu convert_ns=%llu io_ns=%llu reads=%llu read_bytes=%llu copy_calls=%llu copy_ns=%llu wait_calls=%llu wait_pending=%llu audio_wait_calls=%llu audio_wait_ns=%llu\n",
                (unsigned long long)bink_decode.video_frames,(unsigned long long)bink_decode.audio_frames,(unsigned long long)bink_decode.next_ns,
                (unsigned long long)bink_decode.convert_ns,(unsigned long long)bink_decode.io_ns,(unsigned long long)bink_decode.read_calls,(unsigned long long)bink_decode.read_bytes,
                (unsigned long long)bink_movie.copy_calls,(unsigned long long)bink_movie.copy_ns,(unsigned long long)bink_movie.wait_calls,(unsigned long long)bink_movie.wait_pending,
                (unsigned long long)bink_movie.audio_wait_calls,(unsigned long long)bink_movie.audio_wait_ns);
            struct fnv_horizon_audio_metrics audout_metrics;
            if(!fnv_horizon_audio_metrics(audio,&audout_metrics))report("Audout refills=%llu underrun_frames=%llu wait_timeouts=%llu\n",(unsigned long long)audout_metrics.refills,(unsigned long long)audout_metrics.underrun_frames,(unsigned long long)audout_metrics.wait_timeouts);
            u64 core_mask=0,main_ticks[4]={0};s32 preferred_core=-1;Result ticks_result=svcGetThreadCoreMask(&preferred_core,&core_mask,thread.thread.handle);
            u64 allowed_cores=0;Result allowed_result=svcGetInfo(&allowed_cores,InfoType_CoreMask,CUR_PROCESS_HANDLE,0);
            report("CPUAllowed rc=%08lX process_mask=%llX fourth_core=%u\n",(unsigned long)allowed_result,(unsigned long long)allowed_cores,R_SUCCEEDED(allowed_result) && (allowed_cores&8)?1u:0u);
            for(unsigned core=0;core<4 && R_SUCCEEDED(ticks_result);core++)if(core_mask&(1u<<core)){
                ticks_result=svcGetInfo(main_ticks+core,InfoType_ThreadTickCount,thread.thread.handle,core);
                if(R_FAILED(ticks_result))ticks_result=svcGetInfo(main_ticks+core,InfoType_ThreadTickCountDeprecated,thread.thread.handle,core);
            }
            u64 main_total=0;Result total_result=svcGetInfo(&main_total,InfoType_ThreadTickCount,thread.thread.handle,TickCountInfo_Total);
            if(R_FAILED(total_result))total_result=svcGetInfo(&main_total,InfoType_ThreadTickCountDeprecated,thread.thread.handle,TickCountInfo_Total);
            unsigned per_core_valid=R_SUCCEEDED(ticks_result) && (main_ticks[0] || main_ticks[1] || main_ticks[2] || main_ticks[3]);
            report("CPU main_ticks_rc=%08lX per_core_valid=%u total_ticks_rc=%08lX total_ticks=%llu affinity=%llX preferred=%ld ticks_hz=%llu core0=%llu core1=%llu core2=%llu core3=%llu\n",(unsigned long)ticks_result,per_core_valid,(unsigned long)total_result,(unsigned long long)main_total,(unsigned long long)core_mask,(long)preferred_core,(unsigned long long)armGetSystemTickFreq(),(unsigned long long)main_ticks[0],(unsigned long long)main_ticks[1],(unsigned long long)main_ticks[2],(unsigned long long)main_ticks[3]);
            struct fnv_windows_diagnostics diagnostics;fnv_windows_try_diagnostics(process,&diagnostics);
            struct fnv_vm_counters memory={0};int vm_ready=fnv_vm_try_counters(vm,&memory);
            report("Providers available_mask=%02X vm_ready=%d vm_commit=%lu workers=%lu/%lu failures=%llu\n",diagnostics.available,vm_ready,(unsigned long)memory.committed_bytes,(unsigned long)diagnostics.workers.active,(unsigned long)diagnostics.workers.capacity,(unsigned long long)diagnostics.workers.failures);
            if(diagnostics.available&FNV_DIAG_FILES)report("IO reads=%llu bytes=%llu read_ms=%llu wait_ms=%llu errors=%llu opens=%llu\n",(unsigned long long)diagnostics.files.reads,(unsigned long long)diagnostics.files.read_bytes,(unsigned long long)(diagnostics.files.read_ns/1000000),(unsigned long long)(diagnostics.files.io_wait_ns/1000000),(unsigned long long)diagnostics.files.read_errors,(unsigned long long)diagnostics.files.opens);
            if(diagnostics.available&FNV_DIAG_HEAPS)report("Heap blocks=%lu lookup_steps=%llu alloc_steps=%llu\n",(unsigned long)diagnostics.heaps.live_blocks,(unsigned long long)diagnostics.heaps.lookup_steps,(unsigned long long)diagnostics.heaps.allocation_steps);
            if(diagnostics.available&FNV_DIAG_HANDLES)report("Wait handles=%lu timeouts=%llu\n",(unsigned long)diagnostics.handles.waiting_threads,(unsigned long long)diagnostics.handles.timeouts);
            if(diagnostics.available&FNV_DIAG_SYNC)report("Wait sections=%lu live=%lu capacity=%lu\n",(unsigned long)diagnostics.sync.waiting_threads,(unsigned long)diagnostics.sync.live_sections,(unsigned long)plan->process.critical_section_capacity);
            report("Input pad_down_seen=%016llX\n",(unsigned long long)seen_buttons);last=now;
        }
        fnv_engine_watchdog_stage(FNV_MON_SLEEP);svcSleepThread(8000000);
    }
    (void)fnv_host_thread_join(&thread);request_stop("main thread completed");
    for(unsigned i=0;i<500;i++){struct fnv_windows_worker_stats workers;if(fnv_windows_worker_stats(process,&workers)!=FNV_CPU_OK || !workers.active)break;svcSleepThread(10000000);}
    uint32_t audio_close=fnv_horizon_audio_close(audio);report("Session end result=%u audio_close=%lu backing retained until OS exit; graceful_game_shutdown=0\n",execution_result,(unsigned long)audio_close);
    /* OS reclaims owned backing after terminating every thread. No freeing of
     * provider/window/VM state still referenced by guests or pending GPU work. */
    fflush(log_file);svcExitProcess();
}
