#define COBJMACROS
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "dsound8.h"
#include "dsound_streams.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x800000u
#define OUTPUT_AT (BASE+0x80000)
#define DESC (BASE+0x80100)
#define FORMAT (BASE+0x80200)
#define GUID_AT (BASE+0x80300)
#define DATA (BASE+0x81000)
static unsigned char *arena;static struct fnv_dsound8 *sound;static struct fnv_pcm_queue *queue;
static HWND window;static unsigned sample_checks,native_checks,listener_checks;
enum {QUERY,ADDREF,RELEASE,CREATE,CAPS,COOP,CERT,BCAPS,POSITION,FORMAT_GET,STATUS,LOCK,PLAY,SEEK,FORMAT_SET,VOLUME,PAN,FREQUENCY,STOP,UNLOCK,RESTORE,VOLUME_GET,PAN_GET,FREQUENCY_GET,L_ALL_GET,L_DISTANCE_GET,L_DOPPLER_GET,L_ORIENTATION_GET,L_POSITION_GET,L_ROLLOFF_GET,L_VELOCITY_GET,L_ALL_SET,L_DISTANCE_SET,L_DOPPLER_SET,L_ORIENTATION_SET,L_POSITION_SET,L_ROLLOFF_SET,L_VELOCITY_SET,L_COMMIT,S_ALL_GET,S_ANGLES_GET,S_ORIENTATION_GET,S_OUTSIDE_GET,S_MAX_GET,S_MIN_GET,S_MODE_GET,S_POSITION_GET,S_VELOCITY_GET,S_ALL_SET,S_ANGLES_SET,S_ORIENTATION_SET,S_OUTSIDE_SET,S_MAX_SET,S_MIN_SET,S_MODE_SET,S_POSITION_SET,S_VELOCITY_SET};
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t get(uint32_t at){uint32_t v;memcpy(&v,arena+at-BASE,4);return v;}
static int valid_window(void *p,uint32_t hwnd){(void)p;return hwnd==0xb0000001 && IsWindow(window);}
static uint32_t call(int method,const uint32_t *args,unsigned n,int negative){
    struct fnv_cpu_state s,a;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<n;i++)put(s.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;uint32_t entry=method<0?0x401500+(method==-2?32:0):0x402000+(unsigned)method*64;
    enum fnv_cpu_result r=fnv_cpu_execute(entry,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT&&fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"DS call %d: %s\n",method,fault.text);
    assert(r==FNV_CPU_OK&&fnv_cpu_get_state(&a)==FNV_CPU_OK&&a.gpr[4]==s.gpr[4]+4+n*4);return a.gpr[0];
}
static void wave(uint32_t rate,uint32_t channels,uint32_t bits){
    WAVEFORMATEX f={0};f.wFormatTag=1;f.nChannels=(WORD)channels;f.nSamplesPerSec=rate;f.wBitsPerSample=(WORD)bits;f.nBlockAlign=(WORD)(channels*(bits/8));f.nAvgBytesPerSec=rate*f.nBlockAlign;
    memcpy(arena+FORMAT-BASE,&f,18);
}
static uint32_t make(uint32_t root,uint32_t flags,uint32_t bytes){
    memset(arena+DESC-BASE,0,36);put(DESC,36);put(DESC+4,flags);put(DESC+8,bytes);put(DESC+16,flags&1?0:FORMAT);
    uint32_t a[]={root,DESC,OUTPUT_AT,0};assert(!call(CREATE,a,4,0));return get(OUTPUT_AT);
}
static void upload(uint32_t b,uint32_t offset,uint32_t bytes,const unsigned char *source){
    uint32_t a[]={b,offset,bytes,DATA,DATA+4,DATA+8,DATA+12,0};assert(!call(LOCK,a,8,0));
    uint32_t p=get(DATA),n=get(DATA+4),q=get(DATA+8),m=get(DATA+12);assert(n+m==bytes);
    memcpy(arena+p-BASE,source,n);if(m)memcpy(arena+q-BASE,source+n,m);
    uint32_t u[]={b,p,n,q,m};assert(!call(UNLOCK,u,5,0));
}
static void start(uint32_t b,int loop){uint32_t a[]={b,0,0,(uint32_t)loop};assert(!call(PLAY,a,4,0));}
static void release(uint32_t b){uint32_t a[]={b};assert(!call(RELEASE,a,1,0));}
static uint32_t cursor(uint32_t b){uint32_t a[]={b,DATA,DATA+4};assert(!call(POSITION,a,3,0));return get(DATA);}
static void read_exact(int16_t *output,uint32_t frames){assert(fnv_pcm_read(queue,output,frames)==frames);}
static DWORD WINAPI pump_worker(void *p){(void)p;return (DWORD)fnv_dsound8_produce(sound,512);}
static uint32_t bits(float value){uint32_t u;memcpy(&u,&value,4);return u;}
static void listener_compare(uint32_t listener,IDirectSound3DListener *native){
    put(DATA,64);uint32_t a[]={listener,DATA};DS3DLISTENER state={0};state.dwSize=sizeof(state);
    assert(call(L_ALL_GET,a,2,0)==(uint32_t)IDirectSound3DListener_GetAllParameters(native,&state));
    assert(get(DATA)==64);float *v=(float *)((unsigned char *)&state+4);
    for(unsigned i=0;i<15;i++){float actual;uint32_t raw=get(DATA+4+i*4);memcpy(&actual,&raw,4);assert(fabs((double)actual-v[i])<0.000002);}
    listener_checks++;
    for(unsigned k=0;k<3;k++){
        float value=0;uint32_t a[]={listener,DATA};HRESULT hr=k==0?IDirectSound3DListener_GetDistanceFactor(native,&value):k==1?IDirectSound3DListener_GetRolloffFactor(native,&value):IDirectSound3DListener_GetDopplerFactor(native,&value);
        assert(call(k==0?L_DISTANCE_GET:k==1?L_ROLLOFF_GET:L_DOPPLER_GET,a,2,0)==(uint32_t)hr);
        float actual;uint32_t raw=get(DATA);memcpy(&actual,&raw,4);assert(fabs((double)actual-value)<0.000002);listener_checks++;
    }
    for(unsigned k=0;k<2;k++){
        D3DVECTOR value;uint32_t a[]={listener,DATA};HRESULT hr=k?IDirectSound3DListener_GetVelocity(native,&value):IDirectSound3DListener_GetPosition(native,&value);
        assert(call(k?L_VELOCITY_GET:L_POSITION_GET,a,2,0)==(uint32_t)hr);
        assert(!memcmp(arena+DATA-BASE,&value,12));listener_checks++;
    }
    D3DVECTOR front,top;uint32_t orient_args[]={listener,DATA,DATA+16};assert(!call(L_ORIENTATION_GET,orient_args,3,0)&&IDirectSound3DListener_GetOrientation(native,&front,&top)==S_OK);
    float *vectors[2]={(float *)&front,(float *)&top};
    for(unsigned k=0;k<2;k++)for(unsigned i=0;i<3;i++){float actual;uint32_t raw=get(DATA+k*16+i*4);memcpy(&actual,&raw,4);assert(fabs((double)actual-vectors[k][i])<0.000002);}
    listener_checks++;
}
static void listener_test(uint32_t root,IDirectSound8 *native){
    uint32_t primary=make(root,0x91,0);DSBUFFERDESC desc={0};desc.dwSize=sizeof(desc);desc.dwFlags=0x91;IDirectSoundBuffer *np;
    assert(IDirectSound8_CreateSoundBuffer(native,&desc,&np,NULL)==S_OK);
    memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DListener,16);uint32_t qi[]={primary,GUID_AT,OUTPUT_AT};
    assert(!call(QUERY,qi,3,0));uint32_t listener=get(OUTPUT_AT);assert(listener==primary+4);
    IDirectSound3DListener *nl;assert(IDirectSoundBuffer_QueryInterface(np,&IID_IDirectSound3DListener,(void **)&nl)==S_OK);
    memcpy(arena+GUID_AT-BASE,&IID_IUnknown,16);qi[0]=listener;assert(!call(QUERY,qi,3,0)&&get(OUTPUT_AT)==primary);uint32_t ref[]={primary};call(RELEASE,ref,1,0);
    IUnknown *nu=NULL,*pu=NULL;assert(IDirectSound3DListener_QueryInterface(nl,&IID_IUnknown,(void **)&nu)==S_OK&&IDirectSoundBuffer_QueryInterface(np,&IID_IUnknown,(void **)&pu)==S_OK&&nu==pu);IUnknown_Release(nu);IUnknown_Release(pu);
    listener_compare(listener,nl);
    for(unsigned i=0;i<100;i++){
        float x=(float)i-50,y=(float)(i%7),z=(float)(i%11);uint32_t a[]={listener,bits(x),bits(y),bits(z),1};
        assert(call(L_POSITION_SET,a,5,0)==(uint32_t)IDirectSound3DListener_SetPosition(nl,x,y,z,DS3D_DEFERRED));listener_compare(listener,nl);
        uint32_t immediate[]={listener,bits(x+1),bits(y+1),bits(z+1),0};assert(call(L_POSITION_SET,immediate,5,0)==(uint32_t)IDirectSound3DListener_SetPosition(nl,x+1,y+1,z+1,0));
        uint32_t scalar[]={listener,bits((float)(i%10)),0};assert(call(L_ROLLOFF_SET,scalar,3,0)==(uint32_t)IDirectSound3DListener_SetRolloffFactor(nl,(float)(i%10),0));listener_compare(listener,nl);
        uint32_t c[]={listener};assert(call(L_COMMIT,c,1,0)==(uint32_t)IDirectSound3DListener_CommitDeferredSettings(nl));listener_compare(listener,nl);
        uint32_t orient[]={listener,bits(1),0,bits(1),0,bits(1),bits(1),i&1};
        assert(call(L_ORIENTATION_SET,orient,8,0)==(uint32_t)IDirectSound3DListener_SetOrientation(nl,1,0,1,0,1,1,i&1));listener_compare(listener,nl);
        float factor=0.5f+(float)(i%4);uint32_t distance[]={listener,bits(factor),i&1};assert(call(L_DISTANCE_SET,distance,3,0)==(uint32_t)IDirectSound3DListener_SetDistanceFactor(nl,factor,i&1));
        assert(call(L_DOPPLER_SET,distance,3,0)==(uint32_t)IDirectSound3DListener_SetDopplerFactor(nl,factor,i&1));
        assert(call(L_VELOCITY_SET,a,5,0)==(uint32_t)IDirectSound3DListener_SetVelocity(nl,x,y,z,1));
        assert(call(L_COMMIT,c,1,0)==(uint32_t)IDirectSound3DListener_CommitDeferredSettings(nl));listener_compare(listener,nl);
    }
    DS3DLISTENER all={0};all.dwSize=64;assert(IDirectSound3DListener_GetAllParameters(nl,&all)==S_OK);
    all.vPosition.x=123;all.flDistanceFactor=2;all.vOrientFront.x=2;all.vOrientFront.y=0;all.vOrientFront.z=0;all.vOrientTop.x=0;all.vOrientTop.y=3;all.vOrientTop.z=0;
    memcpy(arena+DATA-BASE,&all,64);uint32_t set[]={listener,DATA,1};assert(call(L_ALL_SET,set,3,0)==(uint32_t)IDirectSound3DListener_SetAllParameters(nl,&all,1));listener_compare(listener,nl);
    uint32_t c[]={listener};assert(!call(L_COMMIT,c,1,0)&&IDirectSound3DListener_CommitDeferredSettings(nl)==S_OK);listener_compare(listener,nl);
    for(unsigned k=0;k<3;k++){
        float invalid=k==0?0:-1;uint32_t a[]={listener,bits(invalid),0};HRESULT hr=k==0?IDirectSound3DListener_SetDistanceFactor(nl,invalid,0):k==1?IDirectSound3DListener_SetRolloffFactor(nl,invalid,0):IDirectSound3DListener_SetDopplerFactor(nl,invalid,0);
        assert(call(k==0?L_DISTANCE_SET:k==1?L_ROLLOFF_SET:L_DOPPLER_SET,a,3,0)==(uint32_t)hr);listener_checks++;
    }
    uint32_t zero[]={listener,0,0,0,0,0,0,0};assert(call(L_ORIENTATION_SET,zero,8,0)==(uint32_t)IDirectSound3DListener_SetOrientation(nl,0,0,0,0,0,0,0));listener_compare(listener,nl);
    /* The listener keeps its controlling primary alive after buffer Release. */
    uint32_t drop[]={primary};assert(call(RELEASE,drop,1,0)==IDirectSoundBuffer_Release(np));listener_compare(listener,nl);
    release(listener);IDirectSound3DListener_Release(nl);
}
#include "dsound_spatial_fixture.h"
static void interface_refs_test(uint32_t root,IDirectSound8 *native){
    wave(48000,1,16);uint32_t b=make(root,0x10090,512);
    WAVEFORMATEX f;memcpy(&f,arena+FORMAT-BASE,18);DSBUFFERDESC d={0};
    d.dwSize=sizeof(d);d.dwFlags=0x10090;d.dwBufferBytes=512;d.lpwfxFormat=&f;
    IDirectSoundBuffer *nb=NULL;IDirectSoundBuffer8 *nb8=NULL;IDirectSound3DBuffer *ns=NULL;
    assert(IDirectSound8_CreateSoundBuffer(native,&d,&nb,NULL)==S_OK);
    uint32_t qi[]={b,GUID_AT,OUTPUT_AT};memcpy(arena+GUID_AT-BASE,&IID_IDirectSoundBuffer8,16);
    assert(!call(QUERY,qi,3,0) && get(OUTPUT_AT)==b);
    assert(IDirectSoundBuffer_QueryInterface(nb,&IID_IDirectSoundBuffer8,(void**)&nb8)==S_OK);
    memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DBuffer,16);
    assert(!call(QUERY,qi,3,0));uint32_t s=get(OUTPUT_AT);assert(s==b+4);
    assert(IDirectSoundBuffer_QueryInterface(nb,&IID_IDirectSound3DBuffer,(void**)&ns)==S_OK);
    assert(call(ADDREF,&root,1,0)==IDirectSound8_AddRef(native));
    assert(call(RELEASE,&root,1,0)==IDirectSound8_Release(native));
    assert(call(ADDREF,&b,1,0)==IDirectSoundBuffer_AddRef(nb));
    assert(call(RELEASE,&b,1,0)==IDirectSoundBuffer_Release(nb));
    assert(call(ADDREF,&s,1,0)==IDirectSound3DBuffer_AddRef(ns));
    assert(call(RELEASE,&s,1,0)==IDirectSound3DBuffer_Release(ns));
    assert(call(RELEASE,&b,1,0)==IDirectSoundBuffer8_Release(nb8));
    assert(call(RELEASE,&b,1,0)==IDirectSoundBuffer_Release(nb));
    DWORD mode=99;uint32_t args[]={s,DATA};assert(!call(S_MODE_GET,args,2,0));
    assert(IDirectSound3DBuffer_GetMode(ns,&mode)==S_OK && get(DATA)==mode);
    /* A live tear-off may reacquire its controlling buffer after base zero. */
    qi[0]=s;memcpy(arena+GUID_AT-BASE,&IID_IUnknown,16);IUnknown *identity=NULL;
    assert(!call(QUERY,qi,3,0) && get(OUTPUT_AT)==b);
    assert(IDirectSound3DBuffer_QueryInterface(ns,&IID_IUnknown,(void**)&identity)==S_OK);
    assert(call(RELEASE,&b,1,0)==IUnknown_Release(identity));
    assert(call(RELEASE,&s,1,0)==IDirectSound3DBuffer_Release(ns));
    native_checks+=11;wave(48000,2,16);
}
int main(int argc,char **argv){
    assert(argc==2||argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);assert(fnv_pcm_create(1024,&queue)==FNV_PCM_OK);
    struct fnv_dsound8_config cfg={0};cfg.arena_address=BASE+0x100000;cfg.arena_bytes=0x400000;cfg.output=queue;cfg.initial_focus=1;cfg.valid_window=valid_window;
    assert(fnv_dsound8_create(process,&cfg,&sound)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_dsound8_exports(&n);assert(n==2);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK&&bound==2);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    window=CreateWindowExA(0,"STATIC","FNV audio ABI test",WS_POPUP,0,0,320,200,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
    uint32_t factory[]={0,OUTPUT_AT,0};if(argc==3&&!strcmp(argv[2],"--strict-output")){factory[1]=0;call(-1,factory,3,1);goto finish;}
    assert(!call(-1,factory,3,0));uint32_t root=get(OUTPUT_AT);assert(root);
    assert(!call(-2,factory,3,0));uint32_t other=get(OUTPUT_AT);assert(other!=root);release(other);
    uint32_t coop[]={root,0xb0000001,2};assert(!call(COOP,coop,3,0));
    if(argc==3&&!strcmp(argv[2],"--strict-stale")){release(root);uint32_t a[]={root,OUTPUT_AT};call(CAPS,a,2,1);goto finish;}
    if(argc==3&&(!strcmp(argv[2],"--strict-listener")||!strcmp(argv[2],"--strict-listener-output"))){
        uint32_t p=make(root,0x91,0);memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DListener,16);uint32_t qi[]={p,GUID_AT,OUTPUT_AT};assert(!call(QUERY,qi,3,0));uint32_t l=get(OUTPUT_AT);
        uint32_t drop[]={p};call(RELEASE,drop,1,0);uint32_t args[]={l,DATA};
        if(!strcmp(argv[2],"--strict-listener"))release(l);else args[1]=0;
        put(DATA,64);call(L_ALL_GET,args,2,1);goto finish;
    }
    if(argc==3 && !strcmp(argv[2],"--strict-spatial-output")){
        wave(48000,1,16);uint32_t b=make(root,0xb0,512);memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DBuffer,16);uint32_t qi[]={b,GUID_AT,OUTPUT_AT};assert(!call(QUERY,qi,3,0));uint32_t args[]={get(OUTPUT_AT),0};call(S_ALL_GET,args,2,1);goto finish;
    }
    wave(48000,2,16);
    if(argc==3){if(!strcmp(argv[2],"--strict-format"))arena[FORMAT-BASE]=3;
        memset(arena+DESC-BASE,0,36);put(DESC,36);put(DESC+4,0);put(DESC+8,512);put(DESC+16,FORMAT);
        uint32_t a[]={root,DESC,OUTPUT_AT,0};call(CREATE,a,4,1);goto finish;}
    /* ABI comparisons use a real Windows device, never play native buffers. */
    IDirectSound8 *native=NULL;HRESULT native_result=DirectSoundCreate8(NULL,&native,NULL);
    assert(native_result==S_OK&&native);assert(IDirectSound8_SetCooperativeLevel(native,window,DSSCL_PRIORITY)==S_OK);
    uint32_t self[]={root};assert(call(ADDREF,self,1,0)==IDirectSound8_AddRef(native));assert(call(RELEASE,self,1,0)==IDirectSound8_Release(native));
    memcpy(arena+GUID_AT-BASE,&IID_IDirectSound8,16);uint32_t qi[]={root,GUID_AT,OUTPUT_AT};assert(!call(QUERY,qi,3,0)&&get(OUTPUT_AT)==root);IUnknown *unk;assert(IDirectSound8_QueryInterface(native,&IID_IDirectSound8,(void **)&unk)==S_OK);IUnknown_Release(unk);call(RELEASE,self,1,0);native_checks++;
    put(DATA,96);uint32_t caps[]={root,DATA};DSCAPS nc={0};nc.dwSize=sizeof(nc);assert(call(CAPS,caps,2,0)==(uint32_t)IDirectSound8_GetCaps(native,&nc));assert(get(DATA)==96&&!(get(DATA+4)&DSCAPS_CERTIFIED));native_checks++;
    interface_refs_test(root,native);listener_test(root,native);spatial_test(root,native);
    uint32_t primary=make(root,DSBCAPS_PRIMARYBUFFER,0);uint32_t fmtset[]={primary,FORMAT};assert(!call(FORMAT_SET,fmtset,2,0));release(primary);
    const uint32_t flags=DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME|DSBCAPS_LOCSOFTWARE|DSBCAPS_GETCURRENTPOSITION2;
    uint32_t b=make(root,flags,1024);WAVEFORMATEX nf;memcpy(&nf,arena+FORMAT-BASE,18);DSBUFFERDESC bd={0};bd.dwSize=sizeof(bd);bd.dwFlags=flags;bd.dwBufferBytes=1024;bd.lpwfxFormat=&nf;IDirectSoundBuffer *nb;assert(IDirectSound8_CreateSoundBuffer(native,&bd,&nb,NULL)==S_OK);
    uint32_t fmtget[]={b,DATA,18,DATA+32};WAVEFORMATEX got;DWORD written=0;assert(call(FORMAT_GET,fmtget,4,0)==(uint32_t)IDirectSoundBuffer_GetFormat(nb,&got,18,&written));assert(get(DATA+32)==written&&!memcmp(arena+DATA-BASE,&got,18));native_checks++;
    for(unsigned k=0;k<3;k++){
        uint32_t value[]={b,DATA};LONG nv=0;DWORD hz=0;HRESULT hr=k==0?IDirectSoundBuffer_GetVolume(nb,&nv):k==1?IDirectSoundBuffer_GetPan(nb,&nv):IDirectSoundBuffer_GetFrequency(nb,&hz);
        assert(call(k==0?VOLUME_GET:k==1?PAN_GET:FREQUENCY_GET,value,2,0)==(uint32_t)hr && get(DATA)==(k==2?hz:(uint32_t)nv));native_checks++;
    }
    /* Invalid scalar arguments do not start native playback or mutate state. */
    for(unsigned k=0;k<3;k++){
        uint32_t value[]={b,k==0?1u:k==1?10001u:99u};HRESULT hr=k==0?IDirectSoundBuffer_SetVolume(nb,1):k==1?IDirectSoundBuffer_SetPan(nb,10001):IDirectSoundBuffer_SetFrequency(nb,99);
        assert(call(k==0?VOLUME:k==1?PAN:FREQUENCY,value,2,0)==(uint32_t)hr);native_checks++;
    }
    uint32_t invalidplay[]={b,1,0,0};assert(call(PLAY,invalidplay,4,0)==(uint32_t)IDirectSoundBuffer_Play(nb,1,0,0));native_checks++;
    int16_t samples[512];for(unsigned i=0;i<256;i++){samples[i*2]=(int16_t)((int)i*31-3500);samples[i*2+1]=(int16_t)(2000-(int)i*17);}
    upload(b,0,1024,(unsigned char *)samples);start(b,0);int16_t output[2048];assert(fnv_dsound8_pump(sound,512)==FNV_PCM_OK);read_exact(output,512);
    for(unsigned i=0;i<1024;i++){assert(output[i]==(i<512?samples[i]:0));sample_checks++;}
    uint32_t st[]={b,DATA};assert(!call(STATUS,st,2,0)&&!get(DATA)&&cursor(b)==0);
    /* Loop/producer on an unattached host audio thread. */
    start(b,1);HANDLE worker=CreateThread(NULL,0,pump_worker,NULL,0,NULL);assert(worker&&WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);DWORD code;assert(GetExitCodeThread(worker,&code)&&!code&&CloseHandle(worker));read_exact(output,512);
    for(unsigned i=0;i<1024;i++){assert(output[i]==samples[i%512]);sample_checks++;}assert(cursor(b)==0);
    /* A full PCM queue must not consume playback time. */
    memset(output,0,sizeof(output));assert(fnv_pcm_write(queue,output,1024)==FNV_PCM_OK);assert(fnv_dsound8_pump(sound,128)==FNV_PCM_FULL&&cursor(b)==0);read_exact(output,1024);
    /* Wraparound Lock publishes both segments only on Unlock. */
    int16_t wrap[16]={111,222,333,444,555,666,777,888,999,1111,1222,1333,1444,1555,1666,1777};upload(b,1008,32,(unsigned char *)wrap);
    uint32_t seekargs[]={b,1008};assert(!call(SEEK,seekargs,2,0));assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned i=0;i<16;i++){assert(output[i]==wrap[i]);sample_checks++;}
    uint32_t stopargs[]={b};assert(!call(STOP,stopargs,1,0));release(b);IDirectSoundBuffer_Release(nb);IDirectSound8_Release(native);
    /* 22050Hz unsigned8 mono -> 48000Hz stereo, exact rational-phase model. */
    wave(22050,1,8);b=make(root,flags,32);unsigned char mono[32];for(unsigned i=0;i<32;i++)mono[i]=(unsigned char)(96+i*2);upload(b,0,32,mono);start(b,0);
    assert(fnv_dsound8_pump(sound,128)==FNV_PCM_OK);read_exact(output,128);uint64_t phase=0;
    for(unsigned i=0;i<128;i++){int expected=0;if(phase<32ull*48000){unsigned a=(unsigned)(phase/48000),next=a==31?a:a+1,frac=(unsigned)(phase%48000);int x=((int)mono[a]-128)*256,y=((int)mono[next]-128)*256;expected=(int)(((int64_t)x*(48000-frac)+(int64_t)y*frac)/48000);phase+=22050;}
        assert(output[i*2]==expected&&output[i*2+1]==expected);sample_checks+=2;}
    release(b);
    /* Source formats/rates use a separate scalar rational-phase oracle. */
    const unsigned rates[]={8000,11025,22050,44100,48000};
    for(unsigned r=0;r<5;r++)for(unsigned channels=1;channels<=2;channels++)for(unsigned bits=8;bits<=16;bits+=8){
        unsigned char raw[128];int32_t source[64];unsigned block=channels*(bits/8);
        for(unsigned f=0;f<32;f++)for(unsigned ch=0;ch<channels;ch++){
            unsigned i=f*channels+ch;
            if(bits==8){raw[i]=(unsigned char)(88+(f*3+ch*5)%80);source[i]=((int32_t)raw[i]-128)*256;}
            else {int16_t v=(int16_t)((int)f*211-3000+(int)ch*701);memcpy(raw+i*2,&v,2);source[i]=v;}
        }
        wave(rates[r],channels,bits);b=make(root,flags,32*block);upload(b,0,32*block,raw);start(b,0);
        assert(fnv_dsound8_pump(sound,256)==FNV_PCM_OK);read_exact(output,256);uint64_t model_phase=0;
        for(unsigned f=0;f<256;f++){
            for(unsigned ch=0;ch<2;ch++){int expected=0;
                if(model_phase<32ull*48000){unsigned a=(unsigned)(model_phase/48000),next=a==31?a:a+1,frac=(unsigned)(model_phase%48000),cidx=channels==1?0:ch;
                    expected=(int)(((int64_t)source[a*channels+cidx]*(48000-frac)+(int64_t)source[next*channels+cidx]*frac)/48000);}
                assert(output[f*2+ch]==expected);sample_checks++;
            }
            model_phase+=rates[r];
        }
        release(b);
    }
    /* Two voices mix with saturation, per-channel pan and mute. */
    wave(48000,2,16);uint32_t pair[2];int16_t loud[8]={20000,-20000,20000,-20000,20000,-20000,20000,-20000};
    for(unsigned k=0;k<2;k++){pair[k]=make(root,flags,16);upload(pair[k],0,16,(unsigned char *)loud);start(pair[k],1);}
    assert(!fnv_dsound8_produce(sound,8));read_exact(output,8);for(unsigned i=0;i<16;i++){assert(output[i]==(i&1?-32768:32767));sample_checks++;}
    uint32_t mute[]={pair[1],(uint32_t)-10000};assert(!call(VOLUME,mute,2,0));uint32_t pan[]={pair[0],10000};assert(!call(PAN,pan,2,0));
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned i=0;i<16;i++){assert(output[i]==(i&1?-20000:0));sample_checks++;}
    uint32_t master=make(root,0x81,0);uint32_t master_volume[]={master,(uint32_t)-10000};assert(!call(VOLUME,master_volume,2,0));
    assert(fnv_dsound8_pump(sound,4)==FNV_PCM_OK);read_exact(output,4);for(unsigned i=0;i<8;i++){assert(!output[i]);sample_checks++;}
    master_volume[1]=0;assert(!call(VOLUME,master_volume,2,0));release(master);
    assert(fnv_dsound8_set_focus(sound,0)==FNV_CPU_OK);
    assert(fnv_dsound8_pump(sound,3)==FNV_PCM_OK);read_exact(output,3);
    for(unsigned i=0;i<6;i++){assert(!output[i]);sample_checks++;}
    assert(cursor(pair[0])==12); /* Focus mutes, it does not stop the clock. */
    assert(fnv_dsound8_set_focus(sound,1)==FNV_CPU_OK);
    /* Movie PCM shares saturation with game voices, and rolls back on FULL. */
    int16_t movie[16];for(unsigned i=0;i<16;i++)movie[i]=(int16_t)(i&1?-20000:20000);
    struct fnv_dsound_movie_stats ms;
    assert(fnv_dsound8_movie_write(sound,movie,8)==FNV_PCM_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);
    for(unsigned i=0;i<16;i++)assert(output[i]==(i&1?-32768:20000));
    for(unsigned k=0;k<2;k++)release(pair[k]);release(root);
    assert(fnv_dsound8_movie_reset(sound)==FNV_PCM_OK);
    int16_t *ring=malloc(48000*4);assert(ring);
    for(unsigned f=0;f<48000;f++){ring[f*2]=(int16_t)(f%30000);ring[f*2+1]=(int16_t)-(int)(f%30000);}
    assert(fnv_dsound8_movie_write(sound,ring,48000)==FNV_PCM_OK);
    assert(fnv_dsound8_movie_write(sound,movie,1)==FNV_PCM_FULL);
    memset(output,0,sizeof(output));assert(fnv_pcm_write(queue,output,1024)==FNV_PCM_OK);
    assert(fnv_dsound8_pump(sound,512)==FNV_PCM_FULL);read_exact(output,1024);
    assert(fnv_dsound8_movie_stats(sound,&ms)==FNV_PCM_OK&&ms.queued==48000&&!ms.played);
    assert(fnv_dsound8_movie_pause(sound,1)==FNV_PCM_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned i=0;i<16;i++)assert(!output[i]);
    assert(fnv_dsound8_movie_pause(sound,0)==FNV_PCM_OK);
    assert(fnv_dsound8_set_focus(sound,0)==FNV_CPU_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned i=0;i<16;i++)assert(!output[i]);
    assert(fnv_dsound8_set_focus(sound,1)==FNV_CPU_OK);
    for(unsigned f=0;f<48000;){unsigned n=48000-f<512?48000-f:512;
        assert(fnv_dsound8_pump(sound,n)==FNV_PCM_OK);read_exact(output,n);
        for(unsigned i=0;i<n*2;i++){assert(output[i]==ring[f*2+i]);sample_checks++;}f+=n;}
    assert(fnv_dsound8_movie_stats(sound,&ms)==FNV_PCM_OK&&!ms.queued&&ms.played==48000);
    uint32_t music[2];assert(fnv_dsound_stream_open(sound,music)==FNV_PCM_OK && fnv_dsound_stream_open(sound,music+1)==FNV_PCM_OK);
    int16_t mpcm[16];for(unsigned k=0;k<16;k++)mpcm[k]=20000;
    for(unsigned k=0;k<2;k++){assert(fnv_dsound_stream_write(sound,music[k],mpcm,8)==FNV_PCM_OK);assert(fnv_dsound_stream_control(sound,music[k],0,0,0)==FNV_PCM_OK);}
    assert(fnv_pcm_write(queue,output,1024)==FNV_PCM_OK);assert(fnv_dsound8_pump(sound,8)==FNV_PCM_FULL);read_exact(output,1024);
    struct fnv_dsound_stream_stats music_stats;assert(fnv_dsound_stream_stats(sound,music[0],&music_stats)==FNV_PCM_OK && music_stats.queued==8 && !music_stats.played);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++)assert(output[k]==32767);
    assert(fnv_dsound_stream_control(sound,music[0],1,0,0)==FNV_PCM_OK);
    assert(fnv_dsound_stream_write(sound,music[0],mpcm,8)==FNV_PCM_OK);assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++)assert(!output[k]);
    assert(fnv_dsound_stream_control(sound,music[0],0,-10000,0)==FNV_PCM_OK);assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++)assert(!output[k]);
    assert(fnv_dsound_stream_reset(sound,music[0])==FNV_PCM_OK);assert(fnv_dsound_stream_stats(sound,music[0],&music_stats)==FNV_PCM_OK && !music_stats.queued && !music_stats.played);
    uint32_t stale=music[0];assert(fnv_dsound_stream_close(sound,music[0])==FNV_PCM_OK);assert(fnv_dsound_stream_open(sound,music)==FNV_PCM_OK && music[0]!=stale);
    assert(fnv_dsound_stream_write(sound,stale,mpcm,8)==FNV_PCM_INVALID);assert(fnv_dsound_stream_close(sound,music[0])==FNV_PCM_OK && fnv_dsound_stream_close(sound,music[1])==FNV_PCM_OK);
    /* Advance ring head, then append a full ring spanning both segments. */
    assert(fnv_dsound8_movie_write(sound,movie,8)==FNV_PCM_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);
    assert(fnv_dsound8_movie_write(sound,ring,48000)==FNV_PCM_OK);
    for(unsigned f=0;f<48000;){unsigned n=48000-f<512?48000-f:512;
        assert(fnv_dsound8_pump(sound,n)==FNV_PCM_OK);read_exact(output,n);
        for(unsigned i=0;i<n*2;i++){assert(output[i]==ring[f*2+i]);sample_checks++;}f+=n;}
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned i=0;i<16;i++)assert(!output[i]);
    assert(fnv_dsound8_movie_stats(sound,&ms)==FNV_PCM_OK&&!ms.queued&&ms.played==96008);
    assert(fnv_dsound8_movie_pause(sound,2)==FNV_PCM_INVALID);
    assert(fnv_dsound8_movie_write(sound,NULL,1)==FNV_PCM_INVALID);
    assert(fnv_dsound8_movie_write(sound,ring,48001)==FNV_PCM_INVALID);
    assert(fnv_dsound8_movie_reset(sound)==FNV_PCM_OK);
    uint32_t simultaneous;assert(fnv_dsound_stream_open(sound,&simultaneous)==FNV_PCM_OK);
    assert(fnv_dsound_stream_write(sound,simultaneous,mpcm,8)==FNV_PCM_OK && fnv_dsound_stream_write(sound,simultaneous,mpcm,8)==FNV_PCM_OK);
    assert(fnv_dsound_stream_control(sound,simultaneous,0,0,0)==FNV_PCM_OK && fnv_dsound8_movie_write(sound,movie,8)==FNV_PCM_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++){assert(output[k]==(k&1?0:32767));sample_checks++;}
    assert(fnv_dsound8_movie_reset(sound)==FNV_PCM_OK && fnv_dsound8_set_focus(sound,0)==FNV_CPU_OK);
    assert(fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++)assert(!output[k]);
    assert(fnv_dsound_stream_stats(sound,simultaneous,&music_stats)==FNV_PCM_OK && music_stats.queued==8 && music_stats.played==8);
    assert(fnv_dsound8_set_focus(sound,1)==FNV_CPU_OK && fnv_dsound8_pump(sound,8)==FNV_PCM_OK);read_exact(output,8);for(unsigned k=0;k<16;k++){assert(output[k]==20000);sample_checks++;}
    assert(fnv_dsound_stream_close(sound,simultaneous)==FNV_PCM_OK);free(ring);
finish:
    {struct fnv_dsound_metrics metrics;assert(fnv_dsound8_try_metrics(sound,&metrics)==FNV_PCM_OK);if(argc==2)assert(metrics.pumps && metrics.frames && metrics.full_returns && metrics.silent_voice_frames);}
    assert(DestroyWindow(window));assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_dsound8_destroy(sound)==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK);fnv_pcm_destroy(queue);assert(VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    printf("{\"status\":\"PASS\",\"pcm_samples_compared\":%u,\"windows_ABI_checks\":%u,\"listener_state_checks\":%u,\"unattached_audio_worker\":true,\"full_queue_rolls_back_cursors\":true,\"engine_started\":false}\n",sample_checks,native_checks,listener_checks);return 0;
}
