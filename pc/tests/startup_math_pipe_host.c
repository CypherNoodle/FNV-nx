#include "startup_math_pipe.h"
#include "startup_math_state.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
static void apply(struct fnv_x87_stack s){
    unsigned char record[178];record[0]=record[1]=0;startup_math_pack(record+2,&s);
    enum fnv_x87_stack_result result=FNV_X87_STACK_INVALID;
    assert(fnv_startup_math_pipe_resolve(&s,0,0,&result));
    for(unsigned j=0;j<4;j++)record[88+j]=(unsigned char)((unsigned)result>>(j*8));
    startup_math_pack(record+92,&s);char applied[357];const char *digits="0123456789abcdef";
    for(unsigned j=0;j<178;j++){applied[j*2]=digits[record[j]>>4];applied[j*2+1]=digits[record[j]&15];}applied[356]=0;
    printf("FNV_X87_APPLIED %s\n",applied);fflush(stdout);
}
static unsigned bucket(const struct fnv_x87_stack *s){
    unsigned char key[88]={0};startup_math_pack(key+2,s);uint32_t h=2166136261u;
    for(unsigned i=0;i<88;i++)h=(h^key[i])*16777619u;
    return h&4095u;
}
static DWORD WINAPI run(void *context){
    unsigned id=(unsigned)(uintptr_t)context;
    for(unsigned round=0;round<128;round++){for(unsigned i=0;i<7;i++){
        struct fnv_x87_stack s;memset(&s,0,sizeof s);s.control=0x37f;s.occupied=3;
        s.registers[0]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000)+(uint64_t)id*123456,0x3ffd};
        s.registers[1]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
        unsigned char record[178];record[0]=(unsigned char)(i>=3);record[1]=(unsigned char)(i>=3?i-3:i);startup_math_pack(record+2,&s);
        enum fnv_x87_stack_result result=FNV_X87_STACK_INVALID;assert(fnv_startup_math_pipe_resolve(&s,i>=3,i>=3?i-3:i,&result));
        assert(result==FNV_X87_STACK_OK);
        memset(record+88,0,4);startup_math_pack(record+92,&s);char applied[357];const char *digits="0123456789abcdef";
        for(unsigned j=0;j<178;j++){applied[j*2]=digits[record[j]>>4];applied[j*2+1]=digits[record[j]&15];}applied[356]=0;
        printf("FNV_X87_APPLIED %s\n",applied);fflush(stdout);
    }}return 0;
}
int main(int argc,char **argv){
    assert(argc==2);struct fnv_x87_stack s;memset(&s,0,sizeof s);s.control=0x37f;s.occupied=1;s.registers[0]=(struct fnv_x87_extended){0,0};
    enum fnv_x87_stack_result result=FNV_X87_STACK_INVALID;assert(!fnv_startup_math_pipe_resolve(&s,0,0,&result));
    assert(fnv_startup_math_pipe_open()&&!fnv_startup_math_pipe_open());
    if(!strcmp(argv[1],"memo")){
        apply(s);apply(s);
        struct fnv_x87_stack collision=s;unsigned i;
        for(i=1;i<1000000;i++){collision.registers[7].significand=i;if(bucket(&collision)==bucket(&s))break;}
        assert(i<1000000);apply(collision);apply(collision);apply(s);apply(s);
        struct fnv_x87_stack changed=s;changed.control=0x77f;apply(changed);
        fnv_startup_math_pipe_close();assert(fnv_startup_math_pipe_open());apply(s);
    }else if(!strcmp(argv[1],"threaded")){
        HANDLE a=CreateThread(NULL,0,run,(void*)1,0,NULL),b=CreateThread(NULL,0,run,(void*)2,0,NULL);assert(a&&b);
        assert(WaitForSingleObject(a,10000)==WAIT_OBJECT_0&&WaitForSingleObject(b,10000)==WAIT_OBJECT_0);DWORD x,y;
        assert(GetExitCodeThread(a,&x)&&GetExitCodeThread(b,&y)&&!x&&!y&&CloseHandle(a)&&CloseHandle(b));
    }else{
        if(!strcmp(argv[1],"cached-negative")){apply(s);s.control=0x77f;}
        struct fnv_x87_stack before=s;assert(!fnv_startup_math_pipe_resolve(&s,0,0,&result));assert(!memcmp(&s,&before,sizeof s)&&result==FNV_X87_STACK_INVALID);
        assert(!fnv_startup_math_pipe_resolve(&s,0,0,&result)); /* broken channel stays disabled */
        s.control=0x37f;before=s;assert(!fnv_startup_math_pipe_resolve(&s,0,0,&result));assert(!memcmp(&s,&before,sizeof s));
    }
    fnv_startup_math_pipe_close();assert(!fnv_startup_math_pipe_resolve(&s,0,0,&result));
    puts("{\"status\":\"PASS\",\"engine_started\":false}");return 0;
}
