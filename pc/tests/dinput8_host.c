#define COBJMACROS
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include "dinput8.h"
#include "dinput8_hid.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define IID (BASE+0x80000)
#define GUID_AT (BASE+0x80100)
#define OUTPUT_AT (BASE+0x80200)
#define FORMAT (BASE+0x81000)
#define OBJECTS (BASE+0x82000)
#define GUIDS (BASE+0x84000)
#define STATE (BASE+0x87000)
#define PROP (BASE+0x89000)
#define COUNT_AT (BASE+0x89100)
#define EVENTS (BASE+0x89200)
static unsigned char *arena;
static HWND native_window;
static int focus_value=1;
static unsigned enum_count,enum_limit;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t get(uint32_t at){uint32_t v;memcpy(&v,arena+at-BASE,4);return v;}
static void observe(void){
    uint32_t at=fnv_cpu_argument_u32(0),context=fnv_cpu_argument_u32(1);assert(context==0x1234 && get(at)==580);
    assert(get(at+36)==18 || get(at+36)==19);assert(strstr((char *)arena+at+40-BASE,"FNV virtual "));
    enum_count++;fnv_cpu_return_u32(enum_count<enum_limit,8);
}
static int input_focus(void *context,uint32_t hwnd){(void)context;assert(hwnd==0xb0000001);return focus_value;}
static int valid_window(void *context,uint32_t hwnd){(void)context;return hwnd==0xb0000001 && IsWindow(native_window);}
static uint32_t call(int method,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(method<0?0x401500:0x402000+(unsigned)method*64,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"DI call %d: %s\n",method,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&a)==FNV_CPU_OK && a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
static void format(const DIDATAFORMAT *f){
    put(FORMAT,24);put(FORMAT+4,16);put(FORMAT+8,f->dwFlags);put(FORMAT+12,f->dwDataSize);put(FORMAT+16,f->dwNumObjs);put(FORMAT+20,OBJECTS);
    for(unsigned i=0;i<f->dwNumObjs;i++){
        DIOBJECTDATAFORMAT *o=f->rgodf+i;uint32_t guid=GUIDS+i*16;
        if(o->pguid)memcpy(arena+guid-BASE,o->pguid,16);
        put(OBJECTS+i*16,o->pguid?guid:0);put(OBJECTS+i*16+4,o->dwOfs);put(OBJECTS+i*16+8,o->dwType);put(OBJECTS+i*16+12,o->dwFlags);
    }
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_dinput8_config cfg={0};cfg.instance=BASE;cfg.objects_address=BASE+0x40000;cfg.objects_bytes=4096;cfg.keyboard=cfg.mouse=1;cfg.valid_window=valid_window;if(argc==2)cfg.has_input_focus=input_focus;
    struct fnv_dinput8 *di;assert(fnv_dinput8_create(process,&cfg,&di)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_dinput8_exports(&n);struct fnv_cpu_export catalog[2];assert(n==1);catalog[0]=e[0];catalog[1]=(struct fnv_cpu_export){"di-test.dll","Observe",0,0,observe};
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,catalog,2,&bound)==FNV_NATIVE_OK && bound==2);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    memcpy(arena+IID-BASE,&IID_IDirectInput8A,16);uint32_t factory[]={BASE,0x800,IID,OUTPUT_AT,0};
    if(argc==3 && !strcmp(argv[2],"--strict-version"))factory[1]=0x700;
    if(argc==3 && !strcmp(argv[2],"--strict-aggregation"))factory[4]=BASE;
    if(argc==3 && !strcmp(argv[2],"--strict-output"))factory[3]=BASE+BYTES;
    if(argc==3 && (!strcmp(argv[2],"--strict-version") || !strcmp(argv[2],"--strict-aggregation") || !strcmp(argv[2],"--strict-output"))){call(-1,factory,5,1);goto finish;}
    assert(!call(-1,factory,5,0));uint32_t root=get(OUTPUT_AT);assert(root);
    if(argc==3 && !strcmp(argv[2],"--strict-stale")){uint32_t args[]={root};assert(!call(3,args,1,0));call(2,args,1,1);goto finish;}
    if(argc==3 && !strcmp(argv[2],"--strict-callback")){uint32_t args[]={root,0,0x401b00,0,1};call(0,args,5,1);goto finish;}
    IDirectInput8A *native=NULL;assert(DirectInput8Create(GetModuleHandleA(NULL),0x800,&IID_IDirectInput8A,(void **)&native,NULL)==DI_OK);
    uint32_t args[]={root};assert(call(2,args,1,0)==IDirectInput8_AddRef(native));assert(call(3,args,1,0)==IDirectInput8_Release(native));
    memcpy(arena+GUID_AT-BASE,&IID_IUnknown,16);uint32_t query[]={root,GUID_AT,OUTPUT_AT};assert(!call(1,query,3,0) && get(OUTPUT_AT)==root);IUnknown *unknown;assert(IDirectInput8_QueryInterface(native,&IID_IUnknown,(void **)&unknown)==S_OK);IUnknown_Release(unknown);call(3,args,1,0);
    uint32_t enumerate[]={root,0,0x401c00,0x1234,1};enum_limit=8;assert(!call(0,enumerate,5,0) && enum_count==2);
    enum_count=0;enum_limit=1;assert(!call(0,enumerate,5,0) && enum_count==1);
    enum_count=0;enumerate[1]=4;assert(!call(0,enumerate,5,0) && !enum_count);
    uint64_t usb[4]={UINT64_C(1)<<26,0,0,UINT64_C(1)<<(228-192)};struct fnv_di_snapshot converted={0};converted.dx=77;
    assert(!fnv_di_usb_keyboard(usb,1,&converted) && converted.keys[0x11]==128 && converted.keys[0x9d]==128 && !converted.keys[0x1d] && converted.dx==77);
    usb[0]|=1;assert(fnv_di_usb_keyboard(usb,0x100,&converted)==1 && !converted.keys[0x3a]);
    struct fnv_di_horizon_poll poll={0};struct fnv_di_mouse_sample samples[]={{12,3,4,120,1},{11,2,3,0,0},{10,1,2,0,0}};
    assert(fnv_di_collect_mouse(samples,3,&poll,&converted) && converted.dx==3 && poll.mouse_sample==12);
    assert(fnv_di_collect_mouse(samples,3,&poll,&converted) && !converted.dx && !converted.wheel);
    poll.mouse_sample=9;assert(fnv_di_collect_mouse(samples,3,&poll,&converted) && converted.dx==6 && converted.dy==9);
    samples[0].sample=20;assert(fnv_di_collect_mouse(samples,3,&poll,&converted) && poll.missed_mouse_samples==7);
    samples[1].sample=21;assert(!fnv_di_collect_mouse(samples,3,&poll,&converted));
    memset(&poll,0,sizeof(poll));samples[0]=(struct fnv_di_mouse_sample){0,7,0,0,8};
    assert(fnv_di_collect_mouse(samples,1,&poll,&converted) && converted.dx==7 && converted.buttons[4]==128 && !converted.buttons[3]);
    assert(fnv_di_collect_mouse(samples,1,&poll,&converted) && !converted.dx);
    native_window=CreateWindowExA(0,"STATIC","DirectInput fixture",WS_POPUP,0,0,320,240,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(native_window);
    uint32_t devices[2];
    for(unsigned kind=0;kind<2;kind++){
        const GUID *guid=kind?&GUID_SysMouse:&GUID_SysKeyboard;memcpy(arena+GUID_AT-BASE,guid,16);
        uint32_t status[]={root,GUID_AT};assert(call(5,status,2,0)==(uint32_t)IDirectInput8_GetDeviceStatus(native,guid));
        uint32_t make[]={root,GUID_AT,OUTPUT_AT,0};assert(!call(4,make,4,0));devices[kind]=get(OUTPUT_AT);
        IDirectInputDevice8A *device;assert(IDirectInput8_CreateDevice(native,guid,&device,NULL)==DI_OK);
        for(unsigned csz=24;csz<=44;csz+=20){DIDEVCAPS caps={0};caps.dwSize=csz;put(PROP,csz);uint32_t cap_args[]={devices[kind],PROP};
            assert(call(14,cap_args,2,0)==(uint32_t)IDirectInputDevice8_GetCapabilities(device,&caps));
            assert(get(PROP)==csz && get(PROP+4)==(DIDC_ATTACHED|DIDC_EMULATED) && get(PROP+12)==(kind?3:0) && get(PROP+16)==(kind?8:256));}
        uint32_t state[]={devices[kind],kind?20:256,STATE};unsigned char before[256];memset(before,0xaa,sizeof(before));memcpy(arena+STATE-BASE,before,sizeof(before));
        HRESULT native_hr=IDirectInputDevice8_GetDeviceState(device,state[1],before);
        assert(call(8,state,3,0)==(uint32_t)native_hr && !memcmp(arena+STATE-BASE,before,sizeof(before)));
        if(kind){format(&c_dfDIMouse);uint32_t initial[]={devices[kind],FORMAT};assert(call(9,initial,2,0)==(uint32_t)IDirectInputDevice8_SetDataFormat(device,&c_dfDIMouse));}
        const DIDATAFORMAT *f=kind?&c_dfDIMouse2:&c_dfDIKeyboard;format(f);uint32_t set[]={devices[kind],FORMAT};
        HRESULT hr=IDirectInputDevice8_SetDataFormat(device,f);uint32_t ours=call(9,set,2,0);
        if(ours!=(uint32_t)hr)fprintf(stderr,"format kind=%u flags=%u bytes=%u count=%u ours=%08x native=%08lx\n",kind,(unsigned)f->dwFlags,(unsigned)f->dwDataSize,(unsigned)f->dwNumObjs,ours,(unsigned long)hr);
        assert(ours==(uint32_t)hr && !ours);
        unsigned char saved=arena[GUIDS+8-BASE];arena[GUIDS+8-BASE]^=1;
        assert(call(9,set,2,0)==(uint32_t)DIERR_INVALIDPARAM);arena[GUIDS+8-BASE]=saved;
        uint32_t coop[]={devices[kind],0xb0000001,DISCL_NONEXCLUSIVE|DISCL_BACKGROUND};assert(call(10,coop,3,0)==(uint32_t)IDirectInputDevice8_SetCooperativeLevel(device,native_window,coop[2]));
        DIPROPDWORD pd={0};pd.diph.dwSize=sizeof(pd);pd.diph.dwHeaderSize=sizeof(pd.diph);pd.diph.dwHow=DIPH_DEVICE;pd.dwData=8;
        memcpy(arena+PROP-BASE,&pd,20);uint32_t property_args[]={devices[kind],1,PROP};
        assert(call(12,property_args,3,0)==(uint32_t)IDirectInputDevice8_SetProperty(device,DIPROP_BUFFERSIZE,&pd.diph));
        assert(call(11,property_args,3,0)==(uint32_t)IDirectInputDevice8_GetProperty(device,DIPROP_BUFFERSIZE,&pd.diph) && get(PROP+16)==pd.dwData);
        DWORD native_count=1;DIDEVICEOBJECTDATA native_event={0};
        HRESULT not_acquired=IDirectInputDevice8_GetDeviceData(device,sizeof(native_event),&native_event,&native_count,0);
        put(COUNT_AT,1);uint32_t buffered_read[]={devices[kind],20,EVENTS,COUNT_AT,0};uint32_t buffered_hr=call(13,buffered_read,5,0);if(buffered_hr!=(uint32_t)not_acquired)fprintf(stderr,"buffered before Acquire ours=%08x native=%08lx count=%lu\n",buffered_hr,(unsigned long)not_acquired,(unsigned long)native_count);assert(buffered_hr==(uint32_t)not_acquired);
        native_count=1;not_acquired=IDirectInputDevice8_GetDeviceData(device,16,&native_event,&native_count,0);
        buffered_read[1]=16;put(COUNT_AT,1);assert(call(13,buffered_read,5,0)==(uint32_t)not_acquired);
        uint32_t one[]={devices[kind]};
        if(argc==3 && !strcmp(argv[2],"--strict-foreground")){
            coop[2]=DISCL_NONEXCLUSIVE|DISCL_FOREGROUND;assert(!call(10,coop,3,0));call(6,one,1,1);
            IDirectInputDevice8_Release(device);IDirectInput8_Release(native);assert(DestroyWindow(native_window));goto finish;
        }
        assert(!call(6,one,1,0));assert(call(6,one,1,0)==S_FALSE);assert(call(9,set,2,0)==(uint32_t)DIERR_ACQUIRED);
        IDirectInputDevice8_Release(device);
    }
    unsigned seed=0x52a9;
    for(unsigned i=0;i<1000;i++){
        struct fnv_di_snapshot s={0};seed=seed*1664525u+1013904223u;s.keys[seed%256]=128;s.buttons[seed%8]=128;s.dx=(int32_t)(seed%101)-50;s.dy=(int32_t)(seed%53)-26;s.wheel=i&1?120:-120;
        assert(fnv_dinput8_update(di,&s)==FNV_CPU_OK);
        uint32_t keys[]={devices[0],256,STATE};assert(!call(8,keys,3,0) && !memcmp(arena+STATE-BASE,s.keys,256));
        uint32_t mouse[]={devices[1],20,STATE};assert(!call(8,mouse,3,0));DIMOUSESTATE2 expected={s.dx,s.dy,s.wheel,{0}};memcpy(expected.rgbButtons,s.buttons,8);assert(!memcmp(arena+STATE-BASE,&expected,20));
        assert(!call(8,mouse,3,0));expected.lX=expected.lY=expected.lZ=0;assert(!memcmp(arena+STATE-BASE,&expected,20));
    }
    /* Controlled focus transitions: not a physical foreground-window test. */
    uint32_t unacquire[]={devices[0]};assert(!call(7,unacquire,1,0));
    uint32_t coop_fg[]={devices[0],0xb0000001,DISCL_NONEXCLUSIVE|DISCL_FOREGROUND};assert(!call(10,coop_fg,3,0));
    /* Missing focus probe remains an explicit fault in --strict-foreground. */
    assert(!call(6,unacquire,1,0));
    unsigned char marker[256];memset(marker,0x5a,sizeof(marker));memcpy(arena+STATE-BASE,marker,sizeof(marker));
    uint32_t keyboard_state[]={devices[0],256,STATE};focus_value=0;
    assert(call(8,keyboard_state,3,0)==(uint32_t)DIERR_INPUTLOST && !memcmp(arena+STATE-BASE,marker,sizeof(marker)));
    assert(call(6,unacquire,1,0)==(uint32_t)DIERR_OTHERAPPHASPRIO);
    focus_value=1;assert(call(8,keyboard_state,3,0)==(uint32_t)DIERR_INPUTLOST);
    assert(!call(6,unacquire,1,0) && !call(8,keyboard_state,3,0));
    struct fnv_di_snapshot move={0};move.dx=19;assert(fnv_dinput8_update(di,&move)==FNV_CPU_OK);
    uint32_t mouse_one[]={devices[1]};assert(call(6,mouse_one,1,0)==S_FALSE);
    uint32_t mouse_state[]={devices[1],20,STATE};assert(!call(8,mouse_state,3,0) && get(STATE)==19);
    move.keys[0]=1;assert(fnv_dinput8_update(di,&move)==FNV_CPU_INVALID);
    assert(!call(8,mouse_state,3,0) && get(STATE)==0);
    /* Buffered reads and immediate motion have independent consumption. */
    for(unsigned k=0;k<2;k++){put(COUNT_AT,UINT32_MAX);uint32_t flush[]={devices[k],20,0,COUNT_AT,0};assert(call(13,flush,5,0)<=1);}
    struct fnv_di_snapshot buffered={0};buffered.keys[30]=128;buffered.dx=7;buffered.buttons[0]=128;
    assert(fnv_dinput8_update(di,&buffered)==FNV_CPU_OK);
    put(COUNT_AT,8);uint32_t read_events[]={devices[0],20,EVENTS,COUNT_AT,1};assert(!call(13,read_events,5,0) && get(COUNT_AT)==1 && get(EVENTS)==30 && get(EVENTS+4)==128);
    uint32_t sequence=get(EVENTS+12);
    read_events[2]=BASE+BYTES-20;put(COUNT_AT,UINT32_MAX);assert(!call(13,read_events,5,0) && get(COUNT_AT)==1 && get(BASE+BYTES-20)==30);
    read_events[2]=0;put(COUNT_AT,UINT32_MAX);assert(!call(13,read_events,5,0) && get(COUNT_AT)==1);
    read_events[1]=16;read_events[2]=EVENTS;put(COUNT_AT,8);assert(call(13,read_events,5,0)==(uint32_t)DIERR_INVALIDPARAM);
    read_events[1]=20;read_events[4]=2;put(COUNT_AT,8);assert(call(13,read_events,5,0)==(uint32_t)DIERR_INVALIDPARAM && get(COUNT_AT)==8);
    read_events[4]=1;
put(COUNT_AT,8);assert(!call(13,read_events,5,0) && get(EVENTS+12)==sequence);
    read_events[4]=0;put(COUNT_AT,1);assert(!call(13,read_events,5,0) && get(COUNT_AT)==1);
    put(COUNT_AT,8);assert(!call(13,read_events,5,0) && !get(COUNT_AT));
    read_events[0]=devices[1];put(COUNT_AT,8);assert(!call(13,read_events,5,0) && get(COUNT_AT)==2 && get(EVENTS)==0 && get(EVENTS+4)==7 && get(EVENTS+20)==12 && get(EVENTS+24)==128);
    assert(!call(8,mouse_state,3,0) && get(STATE)==7);
    for(unsigned k=0;k<10;k++){buffered.dx=0;buffered.keys[30]=k&1?128:0;assert(fnv_dinput8_update(di,&buffered)==FNV_CPU_OK);}
    read_events[0]=devices[0];read_events[4]=1;put(COUNT_AT,8);assert(call(13,read_events,5,0)==(uint32_t)DI_BUFFEROVERFLOW && get(COUNT_AT)==8);
    read_events[4]=0;put(COUNT_AT,8);assert(call(13,read_events,5,0)==(uint32_t)DI_BUFFEROVERFLOW);
    put(COUNT_AT,8);assert(!call(13,read_events,5,0) && !get(COUNT_AT));
    for(unsigned i=0;i<2;i++){uint32_t one[]={devices[i]};assert(!call(7,one,1,0));assert(!call(3,one,1,0));}
    assert(!call(3,args,1,0));assert(IDirectInput8_Release(native)==0);assert(DestroyWindow(native_window));
    unsigned remaining=0;
    while(!call(-1,factory,5,0)){uint32_t one[]={get(OUTPUT_AT)};assert(!call(3,one,1,0));remaining++;assert(remaining<64);}
    assert(remaining==61 && !get(OUTPUT_AT));
finish:
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_dinput8_destroy(di)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"keyboard_mouse_snapshots\":1000,\"windows_standard_formats\":3,\"foreground_loss_reacquire\":true,\"buffered_input_checked\":true,\"capability_layouts_checked\":true,\"engine_started\":false}");return 0;
}
