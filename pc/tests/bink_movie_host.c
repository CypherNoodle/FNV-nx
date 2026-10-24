#include <windows.h>
#include "bink_movie.h"
#include "dsound8.h"
#include "host_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0xc00000u
#define PATH_AT (BASE+0x90000)
#define PIXELS (BASE+0x200000)
enum {SET_SOUND,OPEN_SOUND,OPEN,CLOSE,WAIT,PAUSE,DECODE,NEXT,COPY};
struct input {FILE *file;uint64_t size;};
struct fixture {const char *video;uint64_t clock,pcm,hash;unsigned opens,closes;struct fnv_dsound8 *sound;struct fnv_pcm_queue *queue;};
static unsigned char *arena;
static unsigned calls;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t get(uint32_t at){uint32_t n;memcpy(&n,arena+at-BASE,4);return n;}
static uint64_t hash(uint64_t h,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)h=(h^p[i])*1099511628211ull;return h;}
static int read_input(void *ctx,unsigned char *p,int bytes){struct input *i=ctx;size_t n=fread(p,1,(size_t)bytes,i->file);return ferror(i->file)?-1:(int)n;}
static int64_t seek_input(void *ctx,int64_t at,int whence){
    struct input *i=ctx;if(whence==0x10000)return (int64_t)i->size;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?fnv_host_file_tell(i->file):whence==SEEK_END?(int64_t)i->size:-1;
    if(base<0 || (at>0&&base>INT64_MAX-at) || (at<0&&at< -base))return -1;at+=base;
    if(at<0 || (uint64_t)at>i->size || !fnv_host_file_seek(i->file,at))return -1;return at;
}
static int open_io(void *ctx,const char *name,struct fnv_bink_io *io,uint64_t *size){
    struct fixture *f=ctx;if(strcmp(name,"Data\\Video\\Intro.bik"))return 0;struct input *i=calloc(1,sizeof(*i));assert(i);
    i->file=fopen(f->video,"rb");assert(i->file&&fnv_host_file_size(i->file,&i->size));*size=i->size;*io=(struct fnv_bink_io){i,read_input,seek_input};f->opens++;return 1;
}
static void close_io(void *ctx,struct fnv_bink_io *io){struct fixture *f=ctx;struct input *i=io->context;assert(i&&fclose(i->file)==0);free(i);f->closes++;memset(io,0,sizeof(*io));}
static int now_ns(void *ctx,uint64_t *n){*n=((struct fixture*)ctx)->clock;return 1;}
static unsigned pump(struct fixture *f){
    struct fnv_dsound_movie_stats s;assert(fnv_dsound8_movie_stats(f->sound,&s)==FNV_PCM_OK);unsigned n=s.queued<512?s.queued:512;if(!n)return 0;
    int16_t samples[1024];assert(fnv_dsound8_pump(f->sound,n)==FNV_PCM_OK&&fnv_pcm_read(f->queue,samples,n)==n);f->hash=hash(f->hash,(const unsigned char*)samples,n*4);f->pcm+=n;return n;
}
static int wait_audio(void *ctx){struct fixture *f=ctx;f->clock+=1000000;return pump(f)!=0;}
static uint32_t call(unsigned api,const uint32_t *a,unsigned count,int negative){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<count;i++)put(s.gpr[4]+4+i*4,a[i]);struct fnv_cpu_fault fault;
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&fault);calls++;
    if(negative){assert(r==FNV_CPU_GUEST_FAULT&&fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    if(r)fprintf(stderr,"Bink movie call %u: %s\n",api,fault.text);
    assert(r==FNV_CPU_OK&&fnv_cpu_get_state(&after)==FNV_CPU_OK&&after.gpr[4]==s.gpr[4]+4+count*4);return after.gpr[0];
}
int main(int argc,char **argv){
    assert(argc==2||argc==3);struct fixture f={0};f.video=getenv("FNV_TEST_BINK_VIDEO");assert(f.video);f.clock=1000000000;f.hash=14695981039346656037ull;
    unsigned maximum=(unsigned)strtoul(getenv("FNV_TEST_BINK_FRAMES"),NULL,10);assert(maximum>0&&maximum<=3000);
    arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=BYTES;cfg.cpu.code_low=BASE+0x1000;cfg.cpu.code_high=BASE+image.size;cfg.image_bytes=image.size;cfg.process_id=11;cfg.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK&&fnv_pcm_create(1024,&f.queue)==FNV_PCM_OK);
    struct fnv_dsound8_config dc={0};dc.arena_address=BASE+0x100000;dc.arena_bytes=0x10000;dc.output=f.queue;dc.initial_focus=1;assert(fnv_dsound8_create(process,&dc,&f.sound)==FNV_CPU_OK);
    struct fnv_bink_movie_config mc={BASE+0x120000,4096,f.sound,&f,open_io,close_io,now_ns,wait_audio};struct fnv_bink_movie *movie;
    assert(fnv_bink_movie_create(process,&mc,&movie)==FNV_CPU_OK);struct fnv_bink_sound_config sc={movie,fnv_bink_movie_sound_factory};struct fnv_bink_sound *selection;
    assert(fnv_bink_sound_create(process,&sc,&selection)==FNV_CPU_OK&&fnv_bink_movie_bind_sound(movie,selection)==FNV_CPU_OK);
    size_t n,k,bound;const struct fnv_cpu_export *se=fnv_bink_sound_exports(&n),*me=fnv_bink_movie_exports(&k);assert(n==2&&k==7);struct fnv_cpu_export exports[9];memcpy(exports,se,n*sizeof(*se));memcpy(exports+n,me,k*sizeof(*me));
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,9,&bound)==FNV_NATIVE_OK&&bound==9);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(fnv_bink_movie_destroy(movie)==FNV_CPU_BUSY);
    uint32_t set[]={fnv_cpu_import_token(se[1].function),0};assert(set[0]&&call(SET_SOUND,set,2,0)==1);
    memcpy(arena+PATH_AT-BASE,"Data\\Video\\Intro.bik",21);uint32_t args[]={PATH_AT,0};
    if(argc==3&&!strcmp(argv[2],"--strict-flags")){args[1]=0x100;call(OPEN,args,2,1);goto finish;}
    uint32_t h=call(OPEN,args,2,0);assert(h&&get(h)==1280&&get(h+4)==720&&get(h+12)==1&&get(h+16)==UINT32_MAX);
    if(argc==3){
        if(!strcmp(argv[2],"--strict-handle")){uint32_t a[]={h+64};call(WAIT,a,1,1);}
        else if(!strcmp(argv[2],"--strict-header")){put(h,1);call(WAIT,&h,1,1);}
        else if(!strcmp(argv[2],"--strict-next"))call(NEXT,&h,1,1);
        else if(!strcmp(argv[2],"--strict-time")){f.clock=0;call(WAIT,&h,1,1);}
        else if(!strcmp(argv[2],"--strict-owner")){assert(fnv_windows_thread_detach()==FNV_CPU_OK);t.thread_id=23;assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);call(WAIT,&h,1,1);}
        else if(!strcmp(argv[2],"--strict-copy")){uint32_t a[]={h,0,5120,720,0,0,0,0,1280,720,3};call(DECODE,&h,1,0);call(COPY,a,11,1);}
        else assert(0);goto finish;
    }
    uint32_t pitch=get(h)*4+16,height=get(h+4);assert((uint64_t)PIXELS+(uint64_t)pitch*height<=BASE+BYTES);memset(arena+PIXELS-BASE,0xa5,(size_t)pitch*height);
    uint64_t epoch=f.clock,vhash=14695981039346656037ull;unsigned videos=0;
    for(unsigned i=0;i<maximum;i++){
        uint64_t due=(uint64_t)i*get(h+24)*1000000000u/get(h+20);
        if(i){f.clock=epoch+due-1;assert(call(WAIT,&h,1,0)==1);}f.clock=epoch+due;assert(call(WAIT,&h,1,0)==0);
        if(i==2){uint32_t pause[]={h,1};assert(call(PAUSE,pause,2,0)==1);f.clock+=2000000000;assert(call(WAIT,&h,1,0)==1);pause[1]=0;assert(!call(PAUSE,pause,2,0));epoch+=2000000000;}
        assert(!call(DECODE,&h,1,0)&&get(h+12)==i+1&&get(h+16)==i+1);assert(!call(DECODE,&h,1,0));
        uint32_t copy[]={h,PIXELS,pitch,height,0,0,0,0,get(h),height,0x80000003};assert(!call(COPY,copy,11,0));
        for(unsigned y=0;y<height;y++){unsigned char *p=arena+PIXELS-BASE+(size_t)y*pitch;
            for(unsigned x=0;x<get(h);x++){unsigned char rgba[]={p[x*4+2],p[x*4+1],p[x*4],255};assert(!p[x*4+3]);vhash=hash(vhash,rgba,4);}
            for(unsigned x=get(h)*4;x<pitch;x++)assert(p[x]==0xa5);
        }videos++;for(unsigned j=0;j<4;j++)pump(&f);if(i+1<maximum)assert(!call(NEXT,&h,1,0));
    }
    while(pump(&f)){}
    if(get(h+8)==maximum){assert(!call(NEXT,&h,1,0)&&get(h+12)==1&&get(h+16)==maximum);assert(!call(DECODE,&h,1,0)&&get(h+16)==1);}
    assert(!call(CLOSE,&h,1,0)&&f.opens==f.closes);
    printf("{\"status\":\"PASS\",\"video_frames\":%u,\"pcm_frames\":%" PRIu64 ",\"rgba_hash\":\"%016" PRIx64 "\",\"pcm_hash\":\"%016" PRIx64 "\",\"translated_movie_calls\":%u,\"game_started\":false,\"played_on_device\":false}\n",videos,f.pcm,vhash,f.hash,calls);
finish:
    assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_bink_sound_destroy(selection)==FNV_CPU_OK&&fnv_bink_movie_destroy(movie)==FNV_CPU_OK&&f.opens==f.closes&&fnv_dsound8_destroy(f.sound)==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK);fnv_pcm_destroy(f.queue);assert(VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3)puts("strict PASS");return 0;
}
