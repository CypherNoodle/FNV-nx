/* Native applet on the application thread, text edits on the guest owner.
 * This adapter is tied to the supported PC image already checked at startup.
 * It services the shared TextEditMenu, not the player record or save data. */
#include "text_cp1252.h"
static struct {
    atomic_uint phase,reopen; /* 0 idle,1 request,2 result,3 applying,4 applet */
    uint32_t storage,menu,seen;
    char initial[129];unsigned char text[33];int accepted,wait_release;
} native_keyboard;
int fnv_engine_keyboard_is_open(void){return atomic_load_explicit(&native_keyboard.phase,memory_order_acquire)==4;}
static unsigned char *keyboard_memory(uint32_t at,uint32_t bytes,int write){
    unsigned char *p=fnv_cpu_try_memory(at,bytes);
    if(!p)return NULL;
    if((uint64_t)at<(uint64_t)plan->vm.base+plan->vm.bytes && (uint64_t)at+bytes>plan->vm.base && !fnv_vm_access(vm,at,bytes,write))return NULL;
    return p;
}
static uint32_t keyboard_menu(void){
    unsigned char *p=keyboard_memory(0x11daec4,4,0);if(!p)return 0;uint32_t menu=load32(p);
    p=menu?keyboard_memory(menu,0x5c,0):NULL;
    return p && load32(p)==0x1070034 && p[0x22]==1?menu:0;
}
static int keyboard_call(uint32_t menu,uint32_t function,int setter){
    struct fnv_cpu_state state;uint32_t low,high,id;
    if(fnv_cpu_get_state(&state)!=FNV_CPU_OK)return 0;
    fnv_windows_current_stack_bounds(&low,&high,&id);
    if(state.gpr[4]<low || state.gpr[4]>high || state.gpr[4]-low<16384 || (state.gpr[4]&3))return 0;
    uint32_t frame=state.gpr[4]-128;unsigned char *p=fnv_cpu_try_memory(frame,8),*ret=fnv_cpu_try_memory(state.gpr[4],4);
    if(!p || !ret)return 0;
    memcpy(p,ret,4);if(setter){uint32_t at=native_keyboard.storage;memcpy(p+4,&at,4);}
    uint32_t saved=g_ecx;g_ecx=setter?menu+0x34:menu;
    (void)fnv_cpu_invoke_stdcall(function,frame,setter?4:0);g_ecx=saved;return 1;
}
static void keyboard_guest(void *unused){
    (void)unused;
    if(!native_keyboard.storage || !plan || g_fs_base!=plan->main_thread.teb_address)return;
    unsigned phase=atomic_load_explicit(&native_keyboard.phase,memory_order_acquire);
    if(phase==2){
        atomic_store_explicit(&native_keyboard.phase,3,memory_order_release);
        if(native_keyboard.accepted && keyboard_menu()==native_keyboard.menu){
            unsigned char *p=fnv_cpu_try_memory(native_keyboard.storage,33);
            if(p){memcpy(p,native_keyboard.text,33);
                if(!keyboard_call(native_keyboard.menu,0x716a70,1)){atomic_store_explicit(&native_keyboard.phase,2,memory_order_release);return;}
                /* TextEditor cursor is an index into the newly replaced text. */
                p=keyboard_memory(native_keyboard.menu,0x5c,1);
                if(p){uint32_t length=(uint32_t)strlen((const char *)native_keyboard.text);memcpy(p+0x44,&length,4);}
                if(keyboard_call(native_keyboard.menu,0x7e6700,0))report("Keyboard text applied to active editor\n");}
        }else report("Keyboard cancelled or editor changed\n");
        atomic_store_explicit(&native_keyboard.phase,0,memory_order_release);return;
    }
    if(phase)return;
    uint32_t menu=keyboard_menu();if(!menu){native_keyboard.seen=0;return;}
    int reopen=(int)atomic_exchange_explicit(&native_keyboard.reopen,0,memory_order_acq_rel);
    if(menu==native_keyboard.seen && !reopen)return;
    unsigned char *p=keyboard_memory(menu,0x5c,0);if(!p)return;
    uint32_t text=load32(p+0x34);size_t length=(size_t)p[0x38]|((size_t)p[0x39]<<8);if(length>32)length=32;
    p=text?keyboard_memory(text,(uint32_t)length+1,0):NULL;
    if(length && !p)return;
    native_keyboard.initial[0]=0;
    if(p && !fnv_text_to_utf8(p,length,native_keyboard.initial,sizeof(native_keyboard.initial)))return;
    native_keyboard.menu=native_keyboard.seen=menu;
    atomic_store_explicit(&native_keyboard.phase,1,memory_order_release);
}
static SwkbdTextCheckResult keyboard_check(char *text,size_t bytes){
    unsigned char converted[33];if(fnv_text_from_utf8(text,converted,sizeof(converted)))return SwkbdTextCheckResult_OK;
    snprintf(text,bytes,"Usa hasta 32 caracteres latinos, numeros o signos.");return SwkbdTextCheckResult_Bad;
}
static int keyboard_host(u64 down,u64 held){
    const u64 combo=HidNpadButton_L|HidNpadButton_R|HidNpadButton_Plus;
    if((held&combo)==combo && (down&combo))atomic_store_explicit(&native_keyboard.reopen,1,memory_order_release);
    unsigned expected=1;
    if(atomic_compare_exchange_strong_explicit(&native_keyboard.phase,&expected,4,memory_order_acq_rel,memory_order_acquire)){
        SwkbdConfig config;Result result=swkbdCreate(&config,0);char text[129]={0};native_keyboard.accepted=0;
        if(R_SUCCEEDED(result)){
            swkbdConfigMakePresetDefault(&config);swkbdConfigSetType(&config,SwkbdType_Latin);
            swkbdConfigSetHeaderText(&config,"Introducir texto");swkbdConfigSetInitialText(&config,native_keyboard.initial);
            swkbdConfigSetStringLenMax(&config,32);swkbdConfigSetTextCheckCallback(&config,keyboard_check);
            const struct fnv_di_snapshot neutral={0};(void)fnv_dinput8_update(dinput,&neutral);(void)fnv_horizon_pad_clear(controller);
            fnv_engine_watchdog_stage(FNV_MON_APPLET);
            (void)fnv_dsound8_set_focus(sound,0);result=swkbdShow(&config,text,sizeof(text));swkbdClose(&config);
            (void)fnv_dsound8_set_focus(sound,1);
            if(R_SUCCEEDED(result))native_keyboard.accepted=fnv_text_from_utf8(text,native_keyboard.text,sizeof(native_keyboard.text));
        }
        native_keyboard.wait_release=1;
        report("Keyboard applet result=%08lX accepted=%d\n",(unsigned long)result,native_keyboard.accepted);
        atomic_store_explicit(&native_keyboard.phase,2,memory_order_release);
        return 1;
    }
    unsigned phase=atomic_load_explicit(&native_keyboard.phase,memory_order_acquire);if(phase==2 || phase==3)return 1;
    if(native_keyboard.wait_release){if(held&(HidNpadButton_A|HidNpadButton_B|HidNpadButton_Plus))return 1;native_keyboard.wait_release=0;}
    return 0;
}
