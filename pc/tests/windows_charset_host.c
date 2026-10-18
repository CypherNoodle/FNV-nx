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
#define DEFAULT_AT (BASE+0xb001u)
#define USED (BASE+0xb007u)
enum {CONVERT,GET_ERROR,SET_ERROR,ACP,VALID_CP,CP_INFO};
static const unsigned counts[]={8,0,1,0,1,2};
static unsigned char *arena;
static uint32_t checks,random_state=0x14521;
static uint32_t next(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(i*8));}
static uint32_t read32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static enum fnv_cpu_result execute(unsigned api,const uint32_t args[8],struct fnv_cpu_fault *fault,uint32_t *value){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;
    s.gpr[3]=0x3456789a;s.gpr[5]=0x456789ab;s.gpr[6]=0x56789abc;s.gpr[7]=0x6789abcd;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0x401500);
    for(unsigned i=0;i<8;i++)put32(s.gpr[4]+4+i*4,args?args[i]:0);
    enum fnv_cpu_result result=fnv_cpu_execute(0x401500+api*64,fault);
    if(result==FNV_CPU_OK){
        assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+counts[api]*4);
        assert(s.gpr[3]==0x3456789a && s.gpr[5]==0x456789ab && s.gpr[6]==0x56789abc && s.gpr[7]==0x6789abcd);*value=s.gpr[0];
    }
    return result;
}
static uint32_t call(unsigned api,const uint32_t args[8]){struct fnv_cpu_fault fault;uint32_t v;assert(execute(api,args,&fault,&v)==FNV_CPU_OK);return v;}
static void compare(UINT cp,DWORD flags,const WCHAR *text,unsigned stored,int count,int capacity,int custom,int use_flag,int alias,int null_output){
    assert(stored<64 && capacity<=128);uint32_t args[8]={cp,flags,INPUT,(uint32_t)count,null_output?0:alias?INPUT:OUTPUT,(uint32_t)capacity,custom?DEFAULT_AT:0,use_flag?USED:0};
    memset(arena+INPUT-BASE-1,0x5a,132);memset(arena+OUTPUT-BASE-1,0x5a,132);
    unsigned char native_input[132],native_out[132];memset(native_input,0x5a,sizeof(native_input));memset(native_out,0x5a,sizeof(native_out));
    for(unsigned i=0;i<stored;i++){arena[INPUT-BASE+i*2]=(unsigned char)text[i];arena[INPUT-BASE+i*2+1]=(unsigned char)(text[i]>>8);native_input[1+i*2]=(unsigned char)text[i];native_input[2+i*2]=(unsigned char)(text[i]>>8);}
    char replacement=custom==2?0:'!';arena[DEFAULT_AT-BASE]=(unsigned char)replacement;
    put32(USED,0x12345678);BOOL used=0x12345678;
    uint32_t set[8]={0x3456};call(SET_ERROR,set);SetLastError(0x3456);
    int expected=WideCharToMultiByte(cp?cp:1252,flags,(LPCWCH)(void *)(native_input+1),count,
        null_output?NULL:(char *)(alias?native_input+1:native_out+1),capacity,custom?&replacement:NULL,use_flag?&used:NULL);
    DWORD error=GetLastError();uint32_t value=call(CONVERT,args),actual_error=call(GET_ERROR,NULL);
    int equal=value==(uint32_t)expected && actual_error==error && read32(USED)==(uint32_t)used &&
        !memcmp(arena+OUTPUT-BASE-1,native_out,132) && !memcmp(arena+INPUT-BASE-1,native_input,132);
    if(!equal)fprintf(stderr,"case %u cp=%u flags=%x count=%d capacity=%d custom=%d used=%d alias=%d null=%d first=%x expected=%d/%lu/%x actual=%u/%u/%x\n",
        checks,cp,(unsigned)flags,count,capacity,custom,use_flag,alias,null_output,stored?text[0]:0,expected,(unsigned long)error,(unsigned)used,value,actual_error,read32(USED));
    assert(equal);checks++;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t n;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&n)==FNV_NATIVE_OK && n==6);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=BASE+0x11000;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(call(ACP,NULL)==1252);
    if(argc==3){
        uint32_t args[8]={1252,0,INPUT,1,OUTPUT,128,0,0};arena[INPUT-BASE]=0;arena[INPUT-BASE+1]=1;
        const char *expected="best-fit";
        if(!strcmp(argv[2],"--strict-codepage")){args[0]=932;expected="code page";}
        else if(!strcmp(argv[2],"--strict-flags")){args[1]=0x200;expected="flags";}
        else if(!strcmp(argv[2],"--strict-pointer")){args[2]=BASE+ARENA-1;args[1]=0x400;expected="pointer unavailable";}
        struct fnv_cpu_fault fault;uint32_t value;assert(execute(CONVERT,args,&fault,&value)==FNV_CPU_GUEST_FAULT && strstr(fault.text,expected));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    const UINT pages[]={0,1252,65001,1200,999999,UINT32_MAX};unsigned info_checks=0;
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);i++)for(unsigned null_output=0;null_output<2;null_output++){
        unsigned char info[20];memset(info,0x5a,20);memset(arena+OUTPUT-BASE,0x5a,20);
        uint32_t args[8]={pages[i],null_output?0:OUTPUT},seed[8]={0x3456};call(SET_ERROR,seed);SetLastError(0x3456);
        int expected=GetCPInfo(pages[i]?pages[i]:1252,null_output?NULL:(CPINFO *)(void *)info);DWORD err=GetLastError();
        assert(call(CP_INFO,args)==(uint32_t)expected && call(GET_ERROR,NULL)==err && !memcmp(info,arena+OUTPUT-BASE,20));info_checks++;
        call(SET_ERROR,seed);SetLastError(0x3456);expected=IsValidCodePage(pages[i]);err=GetLastError();
        assert(call(VALID_CP,args)==(uint32_t)expected && call(GET_ERROR,NULL)==err);info_checks++;
    }
    /* Other Windows installations may provide more pages. This virtual
     * process advertises only the two implemented encoding families. */
    uint32_t unavailable[8]={932};assert(!call(VALID_CP,unavailable));
    for(unsigned unit=0;unit<65536;unit++){
        WCHAR text[2]={(WCHAR)unit,0};compare(1252,0x400,text,2,1,1,0,1,0,0);compare(65001,0,text,2,1,8,0,0,0,0);
    }
    const WCHAR direct[]={0,1,65,127,160,255,0x20ac,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    for(unsigned trial=0;trial<12000;trial++){
        WCHAR text[64];unsigned stored=1+next()%32;UINT cp=trial%3==0?0:trial%3==1?1252:65001;
        DWORD flags=cp==65001?(trial&1?0x80:0):cp==1252?0x400:0;
        for(unsigned i=0;i<stored;i++)text[i]=(WCHAR)(cp==0?direct[next()%(sizeof(direct)/sizeof(direct[0]))]:next()>>16);
        text[stored++]=0;
        int count=trial%4==0?-1:trial%4==1?-2:(int)stored-1;
        compare(cp,flags,text,stored,count,(int)(next()%100),cp==65001?0:trial%3,cp!=65001,0,0);
    }
    const WCHAR special[]={65,0x20ac,0xd83d,0xde00,0xd800,66,0};
    for(unsigned cp=0;cp<2;cp++)for(int cap=0;cap<20;cap++){
        compare(cp?65001:1252,cp?0x80:0x400,special,7,7,cap,0,!cp,0,0);
        compare(cp?65001:1252,0,special,2,2,cap,0,0,0,0);
    }
    const WCHAR pairs[][3]={{0xd800,0xdc00,0},{0xd800,0xdfff,0},{0xdbff,0xdc00,0},{0xdbff,0xdfff,0}};
    for(unsigned pair=0;pair<4;pair++)for(unsigned strict=0;strict<2;strict++)for(int cap=0;cap<7;cap++)
        compare(65001,strict?0x80:0,pairs[pair],3,-1,cap,0,0,0,0);
    for(unsigned strict=0;strict<2;strict++)for(int cap=0;cap<14;cap++)
        compare(65001,strict?0x80:0,special,7,7,cap,1,1,0,0);
    const WCHAR ascii[]={65,0,66,0};
    for(unsigned cp=0;cp<2;cp++)for(int count=-2;count<=4;count++)for(int cap=-1;cap<=6;cap++){
        compare(cp?65001:1252,0,ascii,4,count,cap,0,0,0,0);
        compare(cp?65001:1252,0,ascii,4,count,cap,0,0,1,0);
        compare(cp?65001:1252,0,ascii,4,count,cap,0,0,0,1);
    }
    compare(65001,0,ascii,4,4,8,1,0,0,0);compare(65001,0,ascii,4,4,8,0,1,0,0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_differential_checks\":%u,\"code_page_info_checks\":%u,\"all_BMP_units_CP1252_no_best_fit_and_UTF8\":true,\"virtual_ACP\":1252,\"native_engine_started\":false}\n",checks,info_checks);return 0;
}
