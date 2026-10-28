#include <windows.h>
#include "windows_thread.h"
#include "windows_profiles.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define SECTION (BASE+0x9001)
#define KEY (BASE+0x9401)
#define NAME (BASE+0x9801)
#define OUTPUT (BASE+0x9C01)
static unsigned char *arena;
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static void text(uint32_t at,const char *s,int wide){unsigned i=0;do{arena[at-BASE+i*(wide?2:1)]=(unsigned char)s[i];if(wide)arena[at-BASE+i*2+1]=0;}while(s[i++]);}
static uint32_t call_args(unsigned api,const uint32_t *arguments,struct fnv_cpu_fault *fault){
    const unsigned counts[]={4,4,0,1,6,4};struct fnv_cpu_state state,got;assert(fnv_cpu_get_state(&state)==FNV_CPU_OK);state.gpr[4]=BASE+0x1BF00;
    assert(fnv_cpu_set_state(&state)==FNV_CPU_OK);write32(state.gpr[4],0x401500);for(unsigned i=0;i<counts[api];i++)write32(state.gpr[4]+4*(i+1),arguments[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*48,fault);
    if(fault){assert(r==FNV_CPU_GUEST_FAULT);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"profile call %u failed\n",api);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&got)==FNV_CPU_OK && got.gpr[4]==state.gpr[4]+4+counts[api]*4);return got.gpr[0];
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d,struct fnv_cpu_fault *fault){uint32_t args[]={a,b,c,d,0,0};return call_args(api,args,fault);}
static uint32_t call_string(uint32_t capacity){uint32_t args[]={SECTION,KEY,BASE+0xA001,OUTPUT,capacity,NAME};return call_args(4,args,NULL);}
static void check_output(const char *expected,uint32_t capacity){
    size_t n=strlen(expected);if(n>=capacity)n=capacity-1;
    assert(call_string(capacity)==n);assert(!memcmp(arena+OUTPUT-BASE,expected,n));assert(arena[OUTPUT-BASE+n]==0);
}
static void save(const char *path,const char *data){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,strlen(data),f)==strlen(data) && !fclose(f));WritePrivateProfileStringA(NULL,NULL,NULL,path);}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    char root[1024],file[1100];assert(GetFullPathNameA(argv[1],sizeof(root),root,NULL));char *end=strrchr(root,'\\');assert(end);*end=0;
    snprintf(file,sizeof(file),"%s/profiles.ini",root);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=8;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count,n;const struct fnv_cpu_export *exports=fnv_windows_profile_exports(&n);assert(n==4);
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&count)==FNV_NATIVE_OK && count==6);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1C000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    text(SECTION,"General",0);text(KEY,"Count",0);text(NAME,".\\profiles.ini",0);save(file,"[General]\nCount=17\n");
    if(argc==3){const char *mode=argv[2];uint32_t sec=SECTION;
        if(!strcmp(mode,"--strict-traversal"))text(NAME,"../profiles.ini",0);
        else if(!strcmp(mode,"--strict-absolute"))text(NAME,"C:\\outside.ini",0);
        else if(!strcmp(mode,"--strict-encoding")){FILE *f=fopen(file,"wb");assert(f);assert(fwrite("\xff\xfe[",1,3,f)==3 && !fclose(f));}
        else if(!strcmp(mode,"--strict-pointer"))sec=BASE+BYTES;
        else {char huge[5000];memset(huge,'A',sizeof(huge)-1);huge[sizeof(huge)-1]=0;save(file,huge);}
        struct fnv_cpu_fault fault;call(0,sec,KEY,777,NAME,&fault);assert(fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    const char *values[]={"12","-12","+12","0x10","010","12abc","abc","","  ","4294967296","4294967295","-4294967297","\"12\"","'12'","12 ;x","0Xff","0x","-0x10","+0x10"," 23  ","0b101","0B101","0o12","0O12","-0b10","0d12","0xFFFFFFFFFF","-4294967296","\"\"","''","\"  \"","\"12","12\"","12.5","123\t45"};
    unsigned comparisons=0,seed=0x47e8;char content[1024],number[180];
    for(unsigned trial=0;trial<2400;trial++){
        seed=seed*1664525u+1013904223u;
        if(trial<sizeof(values)/sizeof(*values))snprintf(number,sizeof(number),"%s",values[trial]);
        else {switch(trial%6){case 0:snprintf(number,sizeof(number),"%u",seed);break;case 1:snprintf(number,sizeof(number),"-%u",seed);break;
            case 2:snprintf(number,sizeof(number),"0x%Xsuffix",seed);break;case 3:snprintf(number,sizeof(number),"0o%o",seed);break;
            case 4:snprintf(number,sizeof(number),"\" %u \"",seed);break;default:snprintf(number,sizeof(number),"0X%X",seed);break;}}
        snprintf(content,sizeof(content)," ;comment\r\n [ General ] suffix\r\n  Count = %s \r\nCount=91\r\n[General]\r\nCount=92\r\n",number);save(file,content);
        for(unsigned wide=0;wide<2;wide++){
            text(SECTION," gEnErAl ",wide);text(KEY," COUNT ",wide);text(NAME,".\\profiles.ini",wide);
            call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);uint32_t expected=GetPrivateProfileIntA(" gEnErAl "," COUNT ",777,file);DWORD error=GetLastError();
            assert(call(wide,SECTION,KEY,777,NAME,NULL)==expected);assert(call(2,0,0,0,0,NULL)==error);comparisons++;
        }
    }
    save(file,"\xff" "ignored\n[General]\nCount=19\n");text(SECTION,"General",0);text(KEY,"Count",0);text(NAME,"profiles.ini",0);
    assert(call(0,SECTION,KEY,777,NAME,NULL)==GetPrivateProfileIntA("General","Count",777,file));
    save(file,"[General]\nOther=8\n[General]\nCount=9\n");text(SECTION,"General",0);text(KEY,"Count",0);text(NAME,"profiles.ini",0);
    assert(call(0,SECTION,KEY,777,NAME,NULL)==GetPrivateProfileIntA("General","Count",777,file));
    assert(call(0,0,KEY,777,NAME,NULL)==GetPrivateProfileIntA(NULL,"Count",777,file));
    assert(call(0,SECTION,0,777,NAME,NULL)==GetPrivateProfileIntA("General",NULL,777,file));
    save(file,"[General]\nName=  Ranger  \nEmpty=\nLong=123456789\n");text(SECTION,"General",0);text(KEY,"Name",0);text(NAME,"profiles.ini",0);text(BASE+0xA001,"fallback",0);
    char expected_string[128];call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);DWORD expected_chars=GetPrivateProfileStringA("General","Name","fallback",expected_string,sizeof(expected_string),file);DWORD expected_error=GetLastError();
    {uint32_t actual=call_string(sizeof(expected_string));if(actual!=expected_chars)fprintf(stderr,"profile string length expected=%lu actual=%u expected_value=[%s] actual_value=[%s]\n",(unsigned long)expected_chars,actual,expected_string,(char *)arena+OUTPUT-BASE);assert(actual==expected_chars);}assert(!strcmp((char *)arena+OUTPUT-BASE,expected_string));{uint32_t actual_error=call(2,0,0,0,0,NULL);if(actual_error!=expected_error)fprintf(stderr,"profile string LastError expected=%lu actual=%u\n",(unsigned long)expected_error,actual_error);assert(actual_error==expected_error);}
    text(KEY,"Missing",0);call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);expected_chars=GetPrivateProfileStringA("General","Missing","fallback",expected_string,sizeof(expected_string),file);expected_error=GetLastError();
    assert(call_string(sizeof(expected_string))==expected_chars);assert(!strcmp((char *)arena+OUTPUT-BASE,expected_string));{uint32_t actual_error=call(2,0,0,0,0,NULL);if(actual_error!=expected_error)fprintf(stderr,"missing profile string LastError expected=%lu actual=%u\n",(unsigned long)expected_error,actual_error);assert(actual_error==expected_error);}
    check_output("fallback",5);
    text(KEY,"Name",0);call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);expected_chars=GetPrivateProfileStringA("General","Name","fallback",expected_string,5,file);expected_error=GetLastError();
    assert(call_string(5)==expected_chars);assert(!strcmp((char *)arena+OUTPUT-BASE,expected_string));{uint32_t actual_error=call(2,0,0,0,0,NULL);if(actual_error!=expected_error)fprintf(stderr,"truncated profile string LastError expected=%lu actual=%u\n",(unsigned long)expected_error,actual_error);assert(actual_error==expected_error);}
    check_output("Ranger",5);
    text(KEY,"Empty",0);call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);expected_chars=GetPrivateProfileStringA("General","Empty","fallback",expected_string,sizeof(expected_string),file);expected_error=GetLastError();
    assert(call_string(sizeof(expected_string))==expected_chars);assert(!strcmp((char *)arena+OUTPUT-BASE,expected_string));assert(call(2,0,0,0,0,NULL)==expected_error);
    check_output("",64);
    const char *string_values[]={"Ranger","  Ranger  ","\"Ranger\"","'Ranger'","\"  Ranger  \"","\"\"","''","\"unfinished","value ;comment","value\t",""};
    unsigned string_comparisons=0;
    for(unsigned i=0;i<sizeof(string_values)/sizeof(*string_values);i++){
        snprintf(content,sizeof(content),"[General]\nName=%s\n",string_values[i]);save(file,content);text(KEY,"Name",0);
        const unsigned capacities[]={1,2,5,128};
        for(unsigned cindex=0;cindex<sizeof(capacities)/sizeof(*capacities);cindex++){
            unsigned capacity=capacities[cindex];memset(expected_string,0x5a,sizeof(expected_string));memset(arena+OUTPUT-BASE,0x5a,sizeof(expected_string));
            call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);
            DWORD n=GetPrivateProfileStringA("General","Name","fallback",expected_string,capacity,file),err=GetLastError();
            uint32_t actual=call_string(capacity);
            if(actual!=n || strcmp((char *)arena+OUTPUT-BASE,expected_string))fprintf(stderr,"string value %u cap %u wanted [%s] got [%s]\n",i,capacity,expected_string,(char *)arena+OUTPUT-BASE);
            uint32_t actual_error=call(2,0,0,0,0,NULL);
            if(actual!=n || memcmp(arena+OUTPUT-BASE,expected_string,sizeof(expected_string)) || actual_error!=err){
                unsigned diff=0;while(diff<sizeof(expected_string) && arena[OUTPUT-BASE+diff]==(unsigned char)expected_string[diff])diff++;
                fprintf(stderr,"string case=%u cap=%u error=%u expected_error=%lu byte_diff=%u\n",i,capacity,actual_error,(unsigned long)err,diff);
            }
            assert(actual==n && !memcmp(arena+OUTPUT-BASE,expected_string,sizeof(expected_string)) && actual_error==err);string_comparisons++;
        }
    }
    const char *defaults[]={NULL,"","fallback","fallback   ","  fallback  ","fallback\t","\"quoted\"  "};
    for(unsigned i=0;i<sizeof(defaults)/sizeof(*defaults);i++){
        text(KEY,"Missing",0);if(defaults[i])text(BASE+0xa001,defaults[i],0);
        memset(expected_string,0x5a,sizeof(expected_string));memset(arena+OUTPUT-BASE,0x5a,sizeof(expected_string));
        call(3,0x1234,0,0,0,NULL);SetLastError(0x1234);
        DWORD n=GetPrivateProfileStringA("General","Missing",defaults[i],expected_string,sizeof(expected_string),file),err=GetLastError();
        uint32_t args[]={SECTION,KEY,defaults[i]?BASE+0xa001:0,OUTPUT,sizeof(expected_string),NAME};
        assert(call_args(4,args,NULL)==n && !memcmp(arena+OUTPUT-BASE,expected_string,sizeof(expected_string)) && call(2,0,0,0,0,NULL)==err);string_comparisons++;
    }
    /* Exercise the translated write ABI against Windows, then compare queries
     * on independent files (not exact formatting, which Windows may normalize). */
    char profile_dir[1100],user_dir[1100],ours[1200],oracle[1200];
    snprintf(user_dir,sizeof(user_dir),"%s/UserData",root);CreateDirectoryA(user_dir,NULL);
    snprintf(user_dir,sizeof(user_dir),"%s/UserData/FNV",root);CreateDirectoryA(user_dir,NULL);
    snprintf(profile_dir,sizeof(profile_dir),"%s/UserData/FNV/profiles",root);CreateDirectoryA(profile_dir,NULL);
    snprintf(ours,sizeof(ours),"%s/write.ini",profile_dir);snprintf(oracle,sizeof(oracle),"%s/oracle.ini",profile_dir);
    const char *initial=";keep\r\n[General]\r\nName=old\r\nOther=keep\r\n[Next]\r\nValue=next\r\n";
    save(ours,initial);save(oracle,initial);
    text(NAME,"C:\\Users\\FNV\\profiles\\write.ini",0);
    const char *sections[]={"General","General","General","Added","Added","General","Missing"};
    const char *keys[]={"Name","Other","New","X",NULL,"Name",NULL};
    const char *strings[]={"new","", "42","value",NULL,NULL,NULL};
    unsigned writes=0;
    for(unsigned i=0;i<7;i++){
        text(SECTION,sections[i],0);if(keys[i])text(KEY,keys[i],0);if(strings[i])text(BASE+0xa001,strings[i],0);
        uint32_t args[]={SECTION,keys[i]?KEY:0,strings[i]?BASE+0xa001:0,NAME};
        BOOL expected_write=WritePrivateProfileStringA(sections[i],keys[i],strings[i],oracle);
        assert(call_args(5,args,NULL)==(uint32_t)expected_write);writes++;
        const char *check_sections[]={"General","Added","Next"},*check_keys[]={"Name","Other","New","X","Value"};
        for(unsigned a=0;a<3;a++)for(unsigned b=0;b<5;b++){
            char wanted[128];GetPrivateProfileStringA(check_sections[a],check_keys[b],"absent",wanted,sizeof(wanted),oracle);
            text(SECTION,check_sections[a],0);text(KEY,check_keys[b],0);text(BASE+0xa001,"absent",0);
            assert(call_string(128)==strlen(wanted) && !strcmp((char *)arena+OUTPUT-BASE,wanted));
        }
    }
    uint32_t flush_args[]={0,0,0,NAME};
    BOOL flushed=WritePrivateProfileStringA(NULL,NULL,NULL,oracle);
    uint32_t actual_flushed=call_args(5,flush_args,NULL);
    if(actual_flushed!=(uint32_t)flushed)fprintf(stderr,"flush expected=%u actual=%u\n",(unsigned)flushed,actual_flushed);
    assert(actual_flushed==(uint32_t)flushed);
    /* First creation, a final line without newline, and malformed encoding. */
    DeleteFileA(ours);DeleteFileA(oracle);
    text(SECTION,"Created",0);text(KEY,"Fresh",0);text(BASE+0xa001,"first",0);
    assert(call(5,SECTION,KEY,BASE+0xa001,NAME,NULL));
    assert(WritePrivateProfileStringA("Created","Fresh","first",oracle));
    char fresh[32];GetPrivateProfileStringA("Created","Fresh","missing",fresh,sizeof(fresh),oracle);
    text(BASE+0xa001,"missing",0);assert(call_string(32)==strlen(fresh) && !strcmp((char*)arena+OUTPUT-BASE,fresh));
    save(ours,"[Created]\nFresh=first");text(KEY,"Second",0);text(BASE+0xa001,"second",0);
    assert(call(5,SECTION,KEY,BASE+0xa001,NAME,NULL));assert(call_string(32)==6 && !strcmp((char*)arena+OUTPUT-BASE,"second"));
    save(ours,"\xff\xfe[");assert(!call(5,SECTION,KEY,BASE+0xa001,NAME,NULL) && call(2,0,0,0,0,NULL)==50);
    FILE *intact=fopen(ours,"rb");assert(intact && fgetc(intact)==255 && fgetc(intact)==254 && fgetc(intact)=='[' && fgetc(intact)==EOF && !fclose(intact));
    /* Read-only game assets and traversal must not be writable. */
    text(SECTION,"General",0);text(KEY,"Name",0);text(BASE+0xa001,"blocked",0);
    text(NAME,"profiles.ini",0);assert(!call(5,SECTION,KEY,BASE+0xa001,NAME,NULL) && call(2,0,0,0,0,NULL)==5);
    text(NAME,"C:\\Users\\FNV\\..\\outside.ini",0);assert(!call(5,SECTION,KEY,BASE+0xa001,NAME,NULL));
    printf("profile write comparisons=%u, query comparisons=105, confined path negatives=2\n",writes);
    text(NAME,"absent.ini",0);char missing[1100];snprintf(missing,sizeof(missing),"%s/absent.ini",root);
    SetLastError(0x1234);uint32_t expected=GetPrivateProfileIntA("General","Count",-1,missing);DWORD error=GetLastError();
    assert(call(0,SECTION,KEY,UINT32_MAX,NAME,NULL)==expected && call(2,0,0,0,0,NULL)==error);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"windows_profile_comparisons\":%u,\"string_edge_comparisons\":%u,\"profile_write_comparisons\":7,\"profile_write_query_comparisons\":105,\"profile_write_edge_cases\":6,\"a_w_queries\":true,\"missing_file_checked\":true,\"native_engine_started\":false}\n",comparisons,string_comparisons);return 0;
}
