#include <windows.h>
#include "windows_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define INPUT (BASE+0x9001u)
#define OUTPUT (BASE+0xa003u)
enum {CLASSIFY,DECODE,MAPPING,GET_ERROR,SET_ERROR};
static const unsigned counts[]={4,6,6,0,1};
static unsigned char *arena;
static uint32_t checks[3],rng=0x35456271;
static uint32_t next(void){rng=rng*1664525u+1013904223u;return rng;}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static enum fnv_cpu_result execute(unsigned api,const uint32_t *args,struct fnv_cpu_fault *fault,uint32_t *value){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    s.gpr[3]=0x3456789a;s.gpr[5]=0x456789ab;s.gpr[6]=0x56789abc;s.gpr[7]=0x6789abcd;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0x401500);
    for(unsigned i=0;i<counts[api];i++)put32(s.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*64,fault);
    if(result==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+counts[api]*4);
        assert(s.gpr[3]==0x3456789a && s.gpr[5]==0x456789ab && s.gpr[6]==0x56789abc && s.gpr[7]==0x6789abcd);*value=s.gpr[0];}
    return result;
}
static uint32_t call(unsigned api,const uint32_t *args){struct fnv_cpu_fault fault;uint32_t value;assert(execute(api,args,&fault,&value)==FNV_CPU_OK);return value;}
static void compare(unsigned api,const unsigned char *input,unsigned bytes,int length,UINT cp,DWORD flags,int capacity,unsigned mode){
    assert(bytes<128 && capacity<=64);unsigned char native_input[132],native_output[132];
    memset(native_input,0x5a,132);memset(native_output,0x5a,132);memcpy(native_input+1,input,bytes);
    memset(arena+INPUT-BASE-1,0x5a,132);memcpy(arena+INPUT-BASE,input,bytes);memset(arena+OUTPUT-BASE-1,0x5a,132);
    uint32_t in=mode&1?0:INPUT,out=mode&2?0:mode&4?INPUT:OUTPUT,args[6];
    if(api==CLASSIFY){args[0]=flags;args[1]=in;args[2]=(uint32_t)length;args[3]=out;}
    else {args[0]=cp;args[1]=flags;args[2]=in;args[3]=(uint32_t)length;args[4]=out;args[5]=(uint32_t)capacity;}
    uint32_t seed[]={0x3456};call(SET_ERROR,seed);SetLastError(0x3456);
    void *host_in=in?native_input+1:NULL,*host_out=out?(mode&4?native_input+1:native_output+1):NULL;
    int expected=api==CLASSIFY?GetStringTypeW(flags,host_in,length,host_out):api==MAPPING?LCMapStringW(cp==0x7f?cp:0x409,flags,host_in,length,host_out,capacity):MultiByteToWideChar(cp?cp:1252,flags,host_in,length,host_out,capacity);
    DWORD error=GetLastError();uint32_t value=call(api,args),actual_error=call(GET_ERROR,NULL);
    int equal=value==(uint32_t)expected && actual_error==error && !memcmp(native_input,arena+INPUT-BASE-1,132) && !memcmp(native_output,arena+OUTPUT-BASE-1,132);
    if(!equal)fprintf(stderr,"api=%u case=%u cp=%u flags=%x length=%d capacity=%d mode=%u first=%x expected=%d/%lu actual=%u/%u\n",
        api,checks[api],cp,(unsigned)flags,length,capacity,mode,bytes?input[0]:0,expected,(unsigned long)error,value,actual_error);
    assert(equal);checks[api]++;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==5);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){
        uint32_t args[6]={1,INPUT,1,OUTPUT,0,0},value;unsigned api=CLASSIFY;const char *text="classification pending";
        arena[INPUT-BASE]=0;arena[INPUT-BASE+1]=4;
        if(!strcmp(argv[2],"--strict-level")){args[0]=2;text="level pending";}
        if(!strcmp(argv[2],"--strict-decode-flags")){api=DECODE;args[0]=1252;args[1]=2;args[2]=INPUT;args[3]=1;args[4]=OUTPUT;args[5]=8;text="decode flags pending";}
        if(!strcmp(argv[2],"--strict-pointer")){args[1]=BASE+ARENA-1;text="pointer unavailable";}
        if(!strcmp(argv[2],"--strict-map-locale") || !strcmp(argv[2],"--strict-map-flags")){
            api=MAPPING;args[0]=0x409;args[1]=0x100;args[2]=INPUT;args[3]=1;args[4]=OUTPUT;args[5]=8;
            if(strstr(argv[2],"locale")){args[0]=0x41f;text="locale pending";}else {args[1]=0x400;text="mapping flags pending";}
        }
        struct fnv_cpu_fault fault;assert(execute(api,args,&fault,&value)==FNV_CPU_GUEST_FAULT && strstr(fault.text,text));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    const uint16_t extra[]={0x20ac,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x17d,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x17e,0x178};
    for(unsigned i=0;i<256+sizeof(extra)/sizeof(extra[0]);i++){
        uint16_t ch=i<256?(uint16_t)i:extra[i-256];unsigned char input[]={ch&255,ch>>8,0,0};
        compare(CLASSIFY,input,4,1,0,1,0,0);compare(CLASSIFY,input,4,-1,0,1,0,0);
        for(unsigned upper=0;upper<2;upper++)for(int cap=0;cap<4;cap++)compare(MAPPING,input,4,1,0x409,upper?0x200:0x100,cap,0);
    }
    for(unsigned trial=0;trial<6000;trial++){
        unsigned char input[128];unsigned units=1+next()%48;
        for(unsigned i=0;i<units;i++){unsigned j=next()%283;uint16_t ch=j<256?(uint16_t)j:extra[j-256];input[i*2]=(unsigned char)ch;input[i*2+1]=(unsigned char)(ch>>8);}
        input[units*2]=input[units*2+1]=0;compare(CLASSIFY,input,units*2+2,trial%2?-2:(int)units,0,1,0,0);
        const UINT locales[]={0,0x409,0x7f,0x400,0x800};
        compare(MAPPING,input,units*2+2,trial%2?-2:(int)units,locales[trial%5],trial%3?0x100:0x200,(int)(next()%55),trial%7==0?4:0);
    }
    for(unsigned b=0;b<256;b++)for(unsigned flags=0;flags<4;flags++)for(int cap=0;cap<4;cap++){
        unsigned char input[]={(unsigned char)b,0};compare(DECODE,input,2,1,1252,(flags&1)|((flags&2)?8:0),cap,0);
    }
    for(unsigned pair=0;pair<65536;pair++)for(unsigned flags=0;flags<2;flags++){
        unsigned char input[]={(unsigned char)pair,(unsigned char)(pair>>8),0};compare(DECODE,input,3,2,65001,flags?8:0,(int)(pair%4),0);
    }
    for(unsigned trial=0;trial<12000;trial++){
        unsigned char input[128];unsigned bytes=1+next()%48;UINT cp=trial%3==0?0:trial%3==1?1252:65001;
        for(unsigned i=0;i<bytes;i++)input[i]=(unsigned char)(next()>>24);input[bytes]=0;
        DWORD flags=cp==65001?(trial&1?8:0):(trial%4==0?0:trial%4==1?1:trial%4==2?8:9);
        compare(DECODE,input,bytes+1,trial%3==0?-1:trial%3==1?-2:(int)bytes,cp,flags,(int)(next()%56),0);
    }
    const unsigned char samples[][8]={{0x41,0xf0,0x9f,0x98,0x80,0x42,0},{0x41,0xe2,0x82,0x42,0},{0xed,0xa0,0x80,0},{0xe0,0x80,0x80,0},{0xf0,0x80,0x80,0x80,0},{0xf4,0x90,0x80,0x80,0}};
    for(unsigned i=0;i<6;i++)for(unsigned flags=0;flags<2;flags++)for(int cap=0;cap<12;cap++)compare(DECODE,samples[i],8,-1,65001,flags?8:0,cap,0);
    const unsigned char ascii[]={65,0,66,0,0,0};
    for(int length=-2;length<=3;length++)for(int cap=-1;cap<=5;cap++)for(unsigned mode=0;mode<5;mode++){
        compare(DECODE,ascii,6,length,1252,1,cap,mode);
        compare(DECODE,ascii,6,length,65001,0,cap,mode);
        if(!mode || mode==1 || mode==2 || mode==4)compare(CLASSIFY,ascii,6,length,0,1,0,mode);
        compare(MAPPING,ascii,6,length,0x409,0x100,cap,mode);
    }
    compare(CLASSIFY,ascii,6,1,0,0,0,0);compare(CLASSIFY,ascii,6,1,0,3,0,0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"classification_checks\":%u,\"byte_to_wide_checks\":%u,\"case_mapping_checks\":%u,\"all_CP1252_bytes\":true,\"all_two_byte_UTF8_inputs\":true,\"native_engine_started\":false}\n",checks[0],checks[1],checks[2]);return 0;
}
