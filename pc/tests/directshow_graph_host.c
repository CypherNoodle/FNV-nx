#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <strmif.h>
#include <control.h>
#include <uuids.h>
#include "directshow_graph.h"
#include "windows_com.h"
#include "dsound_streams.h"
#include "mp3_stream.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x800000u
#define DATA (BASE+0x80000)
#define SOURCE (BASE+0x90000)
#define READER (SOURCE+4)
static unsigned char *arena,*track;static uint32_t track_bytes;
static uint32_t source_refs=1,reader_refs,joined,reads,read_bytes,callbacks;
static void put(uint32_t at,uint32_t n){memcpy(arena+at-BASE,&n,4);}
static uint32_t get(uint32_t at){uint32_t n;memcpy(&n,arena+at-BASE,4);return n;}
static void src_qi(void){assert(fnv_cpu_argument_u32(0)==READER);uint32_t out=fnv_cpu_argument_u32(2);reader_refs++;put(out,READER);callbacks++;fnv_cpu_return_u32(0,12);}
static void src_add(void){assert(fnv_cpu_argument_u32(0)==SOURCE);callbacks++;fnv_cpu_return_u32(++source_refs,4);}
static void src_release(void){uint32_t self=fnv_cpu_argument_u32(0),n;if(self==SOURCE){assert(source_refs>1);n=--source_refs;}else {assert(self==READER && reader_refs);n=--reader_refs;}callbacks++;fnv_cpu_return_u32(n,4);}
static void src_join(void){assert(fnv_cpu_argument_u32(0)==SOURCE);joined=fnv_cpu_argument_u32(1);if(joined)assert(fnv_cpu_argument_u32(2));callbacks++;fnv_cpu_return_u32(0,12);}
static void src_state(void){assert(fnv_cpu_argument_u32(0)==SOURCE);callbacks++;fnv_cpu_return_u32(0,4);}
static void src_run(void){assert(fnv_cpu_argument_u32(0)==SOURCE && !fnv_cpu_argument_u32(1) && !fnv_cpu_argument_u32(2));callbacks++;fnv_cpu_return_u32(0,12);}
static void src_read(void){uint64_t offset=fnv_cpu_argument_u32(1)|(uint64_t)fnv_cpu_argument_u32(2)<<32;uint32_t bytes=fnv_cpu_argument_u32(3),out=fnv_cpu_argument_u32(4);
    assert(fnv_cpu_argument_u32(0)==READER && offset<=track_bytes && bytes<=track_bytes-offset);memcpy(fnv_cpu_memory(out,bytes),track+(size_t)offset,bytes);reads++;read_bytes+=bytes;callbacks++;fnv_cpu_return_u32(0,20);
}
static void src_length(void){assert(fnv_cpu_argument_u32(0)==READER);uint64_t n=track_bytes;memcpy(fnv_cpu_memory(fnv_cpu_argument_u32(1),8),&n,8);memcpy(fnv_cpu_memory(fnv_cpu_argument_u32(2),8),&n,8);callbacks++;fnv_cpu_return_u32(0,12);}
static uint32_t call(int method,const uint32_t *args,unsigned count){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x23e00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put(s.gpr[4],0x401500);
    for(unsigned i=0;i<count;i++)put(s.gpr[4]+4+i*4,args[i]);struct fnv_cpu_fault fault;
    enum fnv_cpu_result r=fnv_cpu_execute(method<0?0x401500+(method==-2?32:0):0x402000+(unsigned)method*64,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"call %d fault %s\n",method,fault.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&after)==FNV_CPU_OK && after.gpr[4]==s.gpr[4]+4+count*4);return after.gpr[0];
}
static uint32_t qi(uint32_t object,const GUID *iid){memcpy(arena+DATA-BASE,iid,16);uint32_t a[]={object,DATA,DATA+32};assert(!call(0,a,3));return get(DATA+32);}
static uint32_t release(uint32_t object){uint32_t a[]={object};return call(2,a,1);}
static double position(uint32_t object,int duration){uint32_t a[]={object,DATA+64};assert(!call(duration?13:15,a,2));double v;memcpy(&v,arena+DATA+64-BASE,8);return v;}
static void seek_to(uint32_t object,double value){uint32_t bits[2];memcpy(bits,&value,8);uint32_t a[]={object,bits[0],bits[1]};assert(!call(14,a,3));}
int main(int argc,char **argv){
    assert(argc==2);const char *filename=getenv("FNV_TEST_MP3");assert(filename);FILE *f=fopen(filename,"rb");assert(f && !fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && size<128*1024*1024 && !fseek(f,0,SEEK_SET));track_bytes=(uint32_t)size;track=malloc(track_bytes);assert(track && fread(track,1,track_bytes,f)==track_bytes && !fclose(f));
    arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=1;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_pcm_queue *queue;assert(fnv_pcm_create(1024,&queue)==FNV_PCM_OK);struct fnv_dsound8 *sound;struct fnv_dsound8_config sc={BASE+0x100000,0x100000,1,NULL,queue,NULL,NULL};assert(fnv_dsound8_create(process,&sc,&sound)==FNV_CPU_OK);
    struct fnv_directshow *show;struct fnv_directshow_config gc={BASE+0x300000,0x90000,NULL,sound};assert(fnv_directshow_create(process,&gc,&show)==FNV_CPU_OK);
    fnv_cpu_function funcs[]={src_qi,src_add,src_release,src_join,src_state,src_run,src_read,src_length};uint32_t tokens[8];assert(fnv_cpu_register_native(fnv_windows_runtime(process),funcs,8,tokens)==FNV_CPU_OK);
    put(SOURCE,SOURCE+0x100);put(READER,SOURCE+0x200);put(SOURCE+0x104,tokens[1]);put(SOURCE+0x108,tokens[2]);put(SOURCE+0x110,tokens[4]);put(SOURCE+0x114,tokens[4]);put(SOURCE+0x118,tokens[5]);put(SOURCE+0x134,tokens[3]);put(SOURCE+0x200,tokens[0]);put(SOURCE+0x208,tokens[2]);put(SOURCE+0x21c,tokens[6]);put(SOURCE+0x220,tokens[7]);
    size_t count,bound;const struct fnv_cpu_export *exports=fnv_windows_com_exports(&count);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,count,&bound)==FNV_NATIVE_OK && bound==2);
    struct fnv_windows_thread_config t={0};t.thread_id=1;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x24000;fnv_cpu_state_init(&t.state,t.stack_high-256,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t init[]={0};assert(!call(-1,init,1));memcpy(arena+DATA-BASE,&CLSID_FilterGraph,16);memcpy(arena+DATA+16-BASE,&IID_IFilterGraph,16);uint32_t a[]={DATA,0,1,DATA+16,DATA+32};assert(!call(-2,a,5));uint32_t graph=get(DATA+32);assert(graph);
    uint32_t builder=qi(graph,&IID_IGraphBuilder),audio=qi(graph,&IID_IBasicAudio),control=qi(graph,&IID_IMediaControl),pos=qi(graph,&IID_IMediaPosition),event=qi(graph,&IID_IMediaEvent);assert(qi(audio,&IID_IUnknown)==graph);release(graph);
    /* Real Windows oracle checks interface availability/canonical identity only. */
    assert(SUCCEEDED(CoInitialize(NULL)));IFilterGraph *native;assert(SUCCEEDED(CoCreateInstance(&CLSID_FilterGraph,NULL,CLSCTX_INPROC_SERVER,&IID_IFilterGraph,(void **)&native)));
    const GUID *iids[]={&IID_IGraphBuilder,&IID_IBasicAudio,&IID_IMediaControl,&IID_IMediaPosition,&IID_IMediaEvent};
    for(unsigned i=0;i<5;i++){IUnknown *n;assert(SUCCEEDED(IFilterGraph_QueryInterface(native,iids[i],(void **)&n)));IUnknown_Release(n);}IFilterGraph_Release(native);CoUninitialize();
    uint32_t unsupported_iid[4]={0x12345678,1,2,3};memcpy(arena+DATA-BASE,unsupported_iid,16);put(DATA+32,1);uint32_t bad_qi[]={graph,DATA,DATA+32};assert(call(0,bad_qi,3)==0x80004002u && !get(DATA+32));
    uint32_t add[]={graph,SOURCE,0};assert(!call(3,add,3) && joined==graph && source_refs==2);uint32_t render[]={builder,READER};assert(!call(4,render,2) && reader_refs==1 && reads);
    double duration=position(pos,1);assert(duration>0);uint32_t state[]={control,0,DATA+64};assert(!call(8,state,3) && !get(DATA+64));
    struct fnv_mp3 *reference;assert(fnv_mp3_open_file(filename,&reference)==FNV_MP3_OK);int16_t expected[1024],actual[1024];unsigned samples=0,nonzero=0;
    uint32_t one[]={control};assert(!call(5,one,1));
    for(unsigned block=0;block<128;block++){uint32_t n;enum fnv_mp3_result r=fnv_mp3_read(reference,expected,512,&n);assert(r==FNV_MP3_OK && n==512);assert(fnv_dsound8_pump(sound,512)==FNV_PCM_OK && fnv_pcm_read(queue,actual,512)==512);assert(!memcmp(expected,actual,sizeof(actual)));samples+=1024;for(unsigned j=0;j<1024;j++)nonzero+=actual[j]!=0;position(pos,0);}
    assert(nonzero>1000);
    double before=position(pos,0);assert(!call(6,one,1));assert(fnv_dsound8_pump(sound,512)==FNV_PCM_OK && fnv_pcm_read(queue,actual,512)==512);for(unsigned i=0;i<1024;i++)assert(!actual[i]);assert(position(pos,0)==before);
    seek_to(pos,0.0);assert(position(pos,0)==0.0);assert(!call(5,one,1));assert(!call(7,one,1));assert(position(pos,0)==0.0);
    uint32_t volume[]={audio,(uint32_t)-10000};assert(!call(9,volume,2));assert(!call(5,one,1));assert(fnv_dsound8_pump(sound,512)==FNV_PCM_OK && fnv_pcm_read(queue,actual,512)==512);for(unsigned i=0;i<1024;i++)assert(!actual[i]);
    assert(!call(7,one,1));seek_to(pos,duration);assert(!call(5,one,1));uint32_t ev[]={event,DATA+64,DATA+68,DATA+72,0};assert(!call(19,ev,5) && get(DATA+64)==1 && !get(DATA+68) && !get(DATA+72));assert(call(19,ev,5)==0x80004004u);
    uint32_t wait[]={event,0,DATA+64};assert(!call(20,wait,3) && get(DATA+64)==1);uint32_t free_ev[]={event,1,0,0};assert(!call(21,free_ev,4));
    release(event);release(pos);release(control);release(audio);release(builder);assert(!release(graph));assert(source_refs==1 && !reader_refs && !joined);fnv_mp3_close(reference);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_directshow_destroy(show)==FNV_CPU_OK && fnv_cpu_unregister_native(fnv_windows_runtime(process),tokens[0],8)==FNV_CPU_OK && fnv_dsound8_destroy(sound)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);fnv_pcm_destroy(queue);free(track);assert(VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"pcm_samples_compared\":%u,\"nonzero_pcm_samples\":%u,\"guest_callbacks\":%u,\"source_reads\":%u,\"source_bytes\":%u,\"duration_seconds\":%.6f,\"windows_interface_queries\":5,\"engine_started\":false}\n",samples,nonzero,callbacks,reads,read_bytes,duration);return 0;
}
