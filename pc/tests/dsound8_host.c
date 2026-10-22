#define COBJMACROS
#define DIRECTSOUND_VERSION 0x0800
#include <windows.h>
#include <dsound.h>
#include "dsound8.h"
#include <stdio.h>
#include <string.h>
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
static HWND window;static unsigned sample_checks,native_checks;
enum {QUERY,ADDREF,RELEASE,CREATE,CAPS,COOP,CERT,BCAPS,POSITION,FORMAT_GET,STATUS,LOCK,PLAY,SEEK,FORMAT_SET,VOLUME,PAN,FREQUENCY,STOP,UNLOCK,RESTORE,VOLUME_GET,PAN_GET,FREQUENCY_GET};
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
    wave(48000,2,16);
    if(argc==3){if(!strcmp(argv[2],"--strict-format"))arena[FORMAT-BASE]=3;
        memset(arena+DESC-BASE,0,36);put(DESC,36);put(DESC+4,!strcmp(argv[2],"--strict-3d")?0x91:0);put(DESC+8,!strcmp(argv[2],"--strict-3d")?0:512);put(DESC+16,!strcmp(argv[2],"--strict-3d")?0:FORMAT);
        uint32_t a[]={root,DESC,OUTPUT_AT,0};call(CREATE,a,4,1);goto finish;}
    /* ABI comparisons use a real Windows device, never play native buffers. */
    IDirectSound8 *native=NULL;HRESULT native_result=DirectSoundCreate8(NULL,&native,NULL);
    assert(native_result==S_OK&&native);assert(IDirectSound8_SetCooperativeLevel(native,window,DSSCL_PRIORITY)==S_OK);
    uint32_t self[]={root};assert(call(ADDREF,self,1,0)==IDirectSound8_AddRef(native));assert(call(RELEASE,self,1,0)==IDirectSound8_Release(native));
    memcpy(arena+GUID_AT-BASE,&IID_IDirectSound8,16);uint32_t qi[]={root,GUID_AT,OUTPUT_AT};assert(!call(QUERY,qi,3,0)&&get(OUTPUT_AT)==root);IUnknown *unk;assert(IDirectSound8_QueryInterface(native,&IID_IDirectSound8,(void **)&unk)==S_OK);IUnknown_Release(unk);call(RELEASE,self,1,0);native_checks++;
    put(DATA,96);uint32_t caps[]={root,DATA};DSCAPS nc={0};nc.dwSize=sizeof(nc);assert(call(CAPS,caps,2,0)==(uint32_t)IDirectSound8_GetCaps(native,&nc));assert(get(DATA)==96&&!(get(DATA+4)&DSCAPS_CERTIFIED));native_checks++;
    uint32_t primary=make(root,DSBCAPS_PRIMARYBUFFER,0);uint32_t fmtset[]={primary,FORMAT};assert(!call(FORMAT_SET,fmtset,2,0));release(primary);
    const uint32_t flags=DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLPAN|DSBCAPS_CTRLVOLUME|DSBCAPS_LOCSOFTWARE;
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
    assert(fnv_dsound8_set_focus(sound,0)==FNV_CPU_OK);
    assert(fnv_dsound8_pump(sound,3)==FNV_PCM_OK);read_exact(output,3);
    for(unsigned i=0;i<6;i++){assert(!output[i]);sample_checks++;}
    assert(cursor(pair[0])==12); /* Focus mutes, it does not stop the clock. */
    assert(fnv_dsound8_set_focus(sound,1)==FNV_CPU_OK);
    for(unsigned k=0;k<2;k++)release(pair[k]);release(root);
finish:
    assert(DestroyWindow(window));assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_dsound8_destroy(sound)==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK);fnv_pcm_destroy(queue);assert(VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    printf("{\"status\":\"PASS\",\"pcm_samples_compared\":%u,\"windows_ABI_checks\":%u,\"unattached_audio_worker\":true,\"full_queue_rolls_back_cursors\":true,\"engine_started\":false}\n",sample_checks,native_checks);return 0;
}
