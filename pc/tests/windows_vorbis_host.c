#include <windows.h>
#include "windows_vorbis.h"
#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>
#include <vorbis/vorbisenc.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define VF 0x480100u
#define OUT_AT 0x490000u
#define SECTION 0x480200u
static unsigned char *arena,encoded[65536];static size_t encoded_bytes;
static struct fnv_windows_process *process;static struct fnv_windows_vorbis *provider;
struct source {size_t position;unsigned reads,seeks,tells,closes;};
static struct source sources[2];
static void put(uint32_t at,uint32_t value){memcpy(arena+at-BASE,&value,4);}
static uint32_t get(uint32_t at){uint32_t value;memcpy(&value,arena+at-BASE,4);return value;}
static void page(ogg_page *p){assert(encoded_bytes+p->header_len+p->body_len<=sizeof(encoded));memcpy(encoded+encoded_bytes,p->header,p->header_len);encoded_bytes+=p->header_len;memcpy(encoded+encoded_bytes,p->body,p->body_len);encoded_bytes+=p->body_len;}
static void encode(void){
    vorbis_info vi;vorbis_info_init(&vi);assert(!vorbis_encode_init_vbr(&vi,2,48000,.3f));
    vorbis_comment vc;vorbis_comment_init(&vc);vorbis_comment_add_tag(&vc,"TITLE","Synthetic callback test");
    vorbis_dsp_state vd;vorbis_block vb;assert(!vorbis_analysis_init(&vd,&vi) && !vorbis_block_init(&vd,&vb));
    ogg_stream_state os;assert(!ogg_stream_init(&os,123));ogg_packet a,b,c;assert(!vorbis_analysis_headerout(&vd,&vc,&a,&b,&c));
    assert(!ogg_stream_packetin(&os,&a) && !ogg_stream_packetin(&os,&b) && !ogg_stream_packetin(&os,&c));ogg_page pg;
    while(ogg_stream_flush(&os,&pg))page(&pg);
    float **pcm=vorbis_analysis_buffer(&vd,4800);assert(pcm);
    for(unsigned i=0;i<4800;i++){pcm[0][i]=((int)(i%97)-48)/128.f;pcm[1][i]=((int)(i%53)-26)/128.f;}
    assert(!vorbis_analysis_wrote(&vd,4800) && !vorbis_analysis_wrote(&vd,0));
    while(vorbis_analysis_blockout(&vd,&vb)==1){assert(!vorbis_analysis(&vb,NULL) && !vorbis_bitrate_addblock(&vb));ogg_packet packet;
        while(vorbis_bitrate_flushpacket(&vd,&packet)){assert(!ogg_stream_packetin(&os,&packet));while(ogg_stream_pageout(&os,&pg))page(&pg);}}
    while(ogg_stream_flush(&os,&pg))page(&pg);
    ogg_stream_clear(&os);vorbis_block_clear(&vb);vorbis_dsp_clear(&vd);vorbis_comment_clear(&vc);vorbis_info_clear(&vi);assert(encoded_bytes>100);
}
static size_t source_read(void *ptr,size_t size,size_t count,void *ctx){struct source *s=ctx;assert(size && count<=SIZE_MAX/size);size_t n=(encoded_bytes-s->position)/size;if(n>count)n=count;memcpy(ptr,encoded+s->position,n*size);s->position+=n*size;s->reads++;return n;}
static int source_seek(void *ctx,ogg_int64_t distance,int origin){struct source *s=ctx;int64_t at=(origin==SEEK_SET?0:origin==SEEK_CUR?(int64_t)s->position:(int64_t)encoded_bytes)+distance;if(at<0 || (uint64_t)at>encoded_bytes)return -1;s->position=(size_t)at;s->seeks++;return 0;}
static int source_close(void *ctx){((struct source*)ctx)->closes++;return 0;}
static long source_tell(void *ctx){struct source *s=ctx;s->tells++;return (long)s->position;}
static struct source *guest_source(uint32_t id){assert(id==1 || id==2);return sources+id-1;}
static void read_guest(void){uint32_t at=fnv_cpu_argument_u32(0),size=fnv_cpu_argument_u32(1),count=fnv_cpu_argument_u32(2),id=fnv_cpu_argument_u32(3);assert(size && count<=UINT32_MAX/size);void *p=fnv_cpu_memory(at,size*count);fnv_cpu_return_u32((uint32_t)source_read(p,size,count,guest_source(id)),16);}
static void seek_guest(void){uint32_t id=fnv_cpu_argument_u32(0),lo=fnv_cpu_argument_u32(1),hi=fnv_cpu_argument_u32(2),origin=fnv_cpu_argument_u32(3);uint64_t bits=(uint64_t)hi<<32|lo;int64_t distance;memcpy(&distance,&bits,8);fnv_cpu_return_u32((uint32_t)source_seek(guest_source(id),distance,(int)origin),16);}
static void close_guest(void){fnv_cpu_return_u32((uint32_t)source_close(guest_source(fnv_cpu_argument_u32(0))),4);}
static void tell_guest(void){fnv_cpu_return_u32((uint32_t)source_tell(guest_source(fnv_cpu_argument_u32(0))),4);}
static enum fnv_cpu_result invoke(unsigned api,unsigned count,const uint32_t *args,struct fnv_cpu_state *out){
    struct fnv_cpu_state state;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=0x41bf00;assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);
    put(state.gpr[4],0x401500+api*64);for(unsigned i=0;i<count;i++)put(state.gpr[4]+4+i*4,args[i]);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&fault);
    if(!r){assert(fnv_cpu_get_state(out)==FNV_CPU_OK && out->gpr[4]==state.gpr[4]+4+count*4);}else fprintf(stderr,"vorbis fixture fault: %s\n",fault.text);return r;
}
static uint32_t call(unsigned api,unsigned n,const uint32_t *args){struct fnv_cpu_state out;assert(invoke(api,n,args,&out)==FNV_CPU_OK);return out.gpr[0];}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);encode();arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=BYTES;cfg.cpu.code_low=0x401000;cfg.cpu.code_high=BASE+image.size;cfg.image_bytes=image.size;cfg.process_id=11;cfg.peb_address=0x408000;
    assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK);struct fnv_vorbis_config vc={0x450000,0x22000,2,NULL};assert(fnv_windows_vorbis_create(process,&vc,&provider)==FNV_CPU_OK);
    size_t n;const struct fnv_cpu_export *list=fnv_windows_vorbis_exports(&n);assert(n==6);struct fnv_cpu_export exports[10];memcpy(exports,list,6*sizeof(*list));
    const char *names[]={"TestRead","TestSeek","TestClose","TestTell"};fnv_cpu_function funcs[]={read_guest,seek_guest,close_guest,tell_guest};for(unsigned i=0;i<4;i++)exports[6+i]=(struct fnv_cpu_export){"vorbis-test.dll",names[i],0,0,funcs[i]};
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t bound;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,10,&bound)==FNV_NATIVE_OK && bound==10);
    struct fnv_windows_thread_config thread={0};thread.thread_id=22;thread.teb_address=0x410000;thread.tls_expansion_address=0x411000;thread.stack_low=0x414000;thread.stack_high=0x41c000;fnv_cpu_state_init(&thread.state,0x41bf00,thread.teb_address);assert(fnv_windows_thread_attach(process,&thread)==FNV_CPU_OK);
    uint32_t open[]={1,VF,0,0,0x401d00,0x401d40,0x401d80,0x401dc0};
    if(argc==3){struct fnv_cpu_state out;if(!strcmp(argv[2],"--strict-callback-abi"))open[4]=0x401cc0;
        else {assert(!call(0,8,open));if(!strcmp(argv[2],"--strict-duplicate")){assert(invoke(0,8,open,&out)==FNV_CPU_GUEST_FAULT);puts("strict PASS");return 0;}
            uint32_t args[]={VF,BASE+BYTES,32,0,2,1,0};assert(invoke(4,7,args,&out)==FNV_CPU_GUEST_FAULT);puts("strict PASS");return 0;}
        assert(invoke(0,8,open,&out)==FNV_CPU_GUEST_FAULT);puts("strict PASS");return 0;}
    unsigned checks=0;
    for(unsigned format=0;format<4;format++){
        memset(sources,0,sizeof(sources));assert(!call(0,8,open));assert(fnv_windows_vorbis_destroy(provider)==FNV_CPU_BUSY);
        struct source native={0};OggVorbis_File reference;ov_callbacks cb={source_read,source_seek,source_close,source_tell};assert(!ov_open_callbacks(&native,&reference,NULL,0,cb));
        uint32_t info[]={VF,UINT32_MAX};uint32_t address=call(5,2,info);vorbis_info *v=ov_info(&reference,-1);assert(address && call(5,2,info)==address && get(address)==(uint32_t)v->version && get(address+4)==(uint32_t)v->channels && get(address+8)==(uint32_t)v->rate);
        struct fnv_cpu_state total;assert(invoke(2,2,info,&total)==FNV_CPU_OK);assert(((uint64_t)total.gpr[2]<<32|total.gpr[0])==(uint64_t)ov_pcm_total(&reference,-1));assert(call(3,1,&open[1])==(uint32_t)ov_seekable(&reference));
        info[1]=1000;assert(invoke(2,2,info,&total)==FNV_CPU_OK && total.gpr[2]==UINT32_MAX && (int32_t)total.gpr[0]==OV_EINVAL);
        unsigned char expected[4096];uint32_t endian=format==1,word=format<2?2:1,sign=format!=3;size_t bytes=0;
        for(unsigned step=0;step<1000;step++){uint32_t read[]={VF,OUT_AT,step%2?4096:128,endian,word,sign,SECTION};int bitstream=-1;long size=ov_read(&reference,(char*)expected,(int)read[2],(int)endian,(int)word,(int)sign,&bitstream);int32_t got=(int32_t)call(4,7,read);assert(got==size);if(got>0){assert(get(SECTION)==(uint32_t)bitstream && !memcmp(arena+OUT_AT-BASE,expected,(size_t)got));bytes+=(size_t)got;checks++;}else {assert(!got);break;}}
        assert(bytes==4800u*2*word);assert(!call(1,1,&open[1]) && !ov_clear(&reference));assert(sources[0].closes==1 && native.closes==1 && sources[0].reads && sources[0].seeks && sources[0].tells);assert((int32_t)call(1,1,&open[1])==OV_EINVAL);
    }
    /* Two independent live streams, capacity failure, and slot reuse. */
    memset(sources,0,sizeof(sources));assert(!call(0,8,open));uint32_t second[8];memcpy(second,open,sizeof(second));second[0]=2;second[1]=VF+0x400;assert(!call(0,8,second));
    uint32_t third[8];memcpy(third,open,sizeof(third));third[1]=VF+0x800;assert((int32_t)call(0,8,third)==OV_EFAULT);
    struct fnv_vorbis_metrics metrics;assert(fnv_windows_vorbis_metrics(&metrics) && metrics.live==2 && metrics.peak==2 && metrics.capacity_denials==1 && metrics.open_failures==1 && metrics.last_error==OV_EFAULT);
    uint32_t first_info[]={VF,UINT32_MAX},second_info[]={VF+0x400,UINT32_MAX};assert(call(5,2,first_info)!=call(5,2,second_info));
    uint32_t read_first[]={VF,OUT_AT,128,0,2,1,SECTION},read_second[]={VF+0x400,OUT_AT+1024,128,0,2,1,SECTION};
    uint32_t a=call(4,7,read_first),b=call(4,7,read_second);assert(a==b && a && !memcmp(arena+OUT_AT-BASE,arena+OUT_AT+1024-BASE,a));
    assert(!call(1,1,&second[1]) && !call(1,1,&open[1]) && sources[0].closes==1 && sources[1].closes==1);
    assert(fnv_windows_vorbis_metrics(&metrics) && !metrics.live);
    /* Failed open does not take ownership or consume a slot. */
    unsigned char saved[65536];memcpy(saved,encoded,encoded_bytes);memset(encoded,0,encoded_bytes);memset(sources,0,sizeof(sources));assert((int32_t)call(0,8,open)==OV_ENOTVORBIS && !sources[0].closes);memcpy(encoded,saved,encoded_bytes);
    /* Stream-only and no-close callbacks preserve source ownership. */
    memset(sources,0,sizeof(sources));open[5]=open[6]=open[7]=0;assert(!call(0,8,open) && !call(3,1,&open[1]));assert(!call(1,1,&open[1]) && !sources[0].closes);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_vorbis_destroy(provider)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"pcm_chunk_comparisons\":%u,\"formats\":4,\"audio_played\":false,\"game_started\":false}\n",checks);return 0;
}
