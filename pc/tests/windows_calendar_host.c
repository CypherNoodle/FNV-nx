#define main fnv_existing_files_main
#include "windows_files_host.c"
#undef main
#include "host_calendar.h"
#include "timezone_rule_test.inc"
static uint32_t calendar_call(unsigned api,uint32_t a,uint32_t b,struct fnv_cpu_fault *fault,enum fnv_cpu_result *result){
    static const unsigned argc[]={2,2,0,1,1,1};struct fnv_cpu_state s;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);uint32_t stack=s.fs_base+0xb000;s.gpr[4]=stack;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(stack,0x401500);put32(stack+4,a);put32(stack+8,b);
    *result=fnv_cpu_execute(0x401500+api*64,fault);if(*result!=FNV_CPU_OK)return 0;
    assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+argc[api]*4);return s.gpr[0];
}
static uint32_t K(unsigned api,uint32_t a,uint32_t b){
    struct fnv_cpu_fault f;enum fnv_cpu_result r;uint32_t v=calendar_call(api,a,b,&f,&r);
    if(r!=FNV_CPU_OK)fprintf(stderr,"calendar %u: %s\n",api,f.text);assert(r==FNV_CPU_OK);return v;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==6);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){struct fnv_cpu_fault f;enum fnv_cpu_result r;
        if(!strcmp(argv[2],"--strict-zone-output"))(void)calendar_call(4,BASE+ARENA-171,0,&f,&r);
        else (void)calendar_call(1,!strcmp(argv[2],"--strict-input")?BASE+ARENA-4:BUFFER, !strcmp(argv[2],"--strict-output")?BASE+ARENA-4:OUTPUT,&f,&r);
        assert(r==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    unsigned timezone_checks=timezone_rule_checks();
    for(unsigned i=0;i<128;i++){
        SYSTEMTIME before,after,actual;FILETIME a,b,c;
        GetLocalTime(&before);memset(arena+OUTPUT-BASE,0xa5,18);K(3,0x8765,0);K(5,OUTPUT+1,0);GetLocalTime(&after);
        memcpy(&actual,arena+OUTPUT+1-BASE,16);
        assert(SystemTimeToFileTime(&before,&a) && SystemTimeToFileTime(&actual,&b) && SystemTimeToFileTime(&after,&c));
        uint64_t lo=(uint64_t)a.dwLowDateTime|((uint64_t)a.dwHighDateTime<<32),at=(uint64_t)b.dwLowDateTime|((uint64_t)b.dwHighDateTime<<32),hi=(uint64_t)c.dwLowDateTime|((uint64_t)c.dwHighDateTime<<32);
        assert(at>=lo && at<=hi && actual.wDayOfWeek==before.wDayOfWeek && K(2,0,0)==0x8765);
        assert(arena[OUTPUT-BASE]==0xa5 && arena[OUTPUT+17-BASE]==0xa5);
    }
    for(unsigned i=0;i<128;++i){TIME_ZONE_INFORMATION wanted={0};SetLastError(0x8765);DWORD state=GetTimeZoneInformation(&wanted),error=GetLastError();assert(state!=TIME_ZONE_ID_INVALID);
        uint32_t dst=OUTPUT+1;memset(arena+OUTPUT-BASE,0xa5,174);K(3,0x8765,0);
        assert(K(4,dst,0)==state && K(2,0,0)==error && !memcmp(arena+dst-BASE,&wanted,172));
        assert(arena[OUTPUT-BASE]==0xa5 && arena[OUTPUT-BASE+173]==0xa5);timezone_checks++;}
    const uint64_t edges[]={0,1,9999,10000,9999999,10000000,864000000000ULL-1,864000000000ULL,
        116444736000000000ULL,0x7fffffffffffffffULL,0x8000000000000000ULL,UINT64_MAX};
    uint64_t seed=0x95728fe03ULL;unsigned checks=0;
    for(unsigned n=0;n<10012;++n){seed=seed*6364136223846793005ULL+1;uint64_t value=n<12?edges[n]:seed;
        for(unsigned api=0;api<2;++api)for(unsigned overlap=0;overlap<3;++overlap){
            uint32_t src=BUFFER+1,dst=overlap==0?OUTPUT+1:overlap==1?src:src+3;unsigned bytes=api?16:8;
            memset(arena+BUFFER-BASE,0xa5,64);memset(arena+OUTPUT-BASE,0xa5,64);put32(src,(uint32_t)value);put32(src+4,(uint32_t)(value>>32));
            unsigned char expected[16]={0};memcpy(expected,arena+dst-BASE,bytes);FILETIME in={(DWORD)value,(DWORD)(value>>32)};
            FILETIME local;SYSTEMTIME system;memcpy(&local,expected,8);memcpy(&system,expected,16);
            SetLastError(0x8765);BOOL wanted=api?FileTimeToSystemTime(&in,&system):FileTimeToLocalFileTime(&in,&local);DWORD error=GetLastError();
            if(wanted)memcpy(expected,api?(const void *)&system:(const void *)&local,bytes);
            unsigned char before=arena[dst-BASE-1],after=arena[dst-BASE+bytes];K(3,0x8765,0);
            assert(K(api,src,dst)==(uint32_t)wanted && K(2,0,0)==error);
            assert(!memcmp(arena+dst-BASE,expected,bytes));assert(arena[dst-BASE-1]==before && arena[dst-BASE+bytes]==after);checks++;
        }
    }
    /* Calendar boundaries independently constructed by the Windows inverse. */
    unsigned leap_checks=0;
    for(unsigned year=1601;year<=30827;year+=7)for(unsigned month=1;month<=12;++month){
        SYSTEMTIME s={(WORD)year,(WORD)month,0,1,23,59,59,999},got;FILETIME file;uint16_t fields[8];
        assert(SystemTimeToFileTime(&s,&file) && FileTimeToSystemTime(&file,&got));
        assert(fnv_calendar_systemtime((uint64_t)file.dwLowDateTime|((uint64_t)file.dwHighDateTime<<32),fields) && !memcmp(fields,&got,16));leap_checks++;
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"translated_oracle_checks\":%u,\"month_boundary_checks\":%u,\"timezone_checks\":%u,\"native_engine_started\":false}\n",checks,leap_checks,timezone_checks);return 0;
}
