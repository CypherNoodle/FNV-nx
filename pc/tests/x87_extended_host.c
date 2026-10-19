#include "x87_extended.h"
#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#endif

#ifdef _WIN32
static DWORD WINAPI worker(LPVOID argument) {
    unsigned rc=(unsigned)(uintptr_t)argument;
    for(unsigned i=0;i<10000;i++) {
        struct fnv_x87_extended a={UINT64_C(0x8000000000000000),0x3fff};
        struct fnv_x87_extended b={UINT64_C(0x8000000000000000),0x3fbf},r;
        uint16_t raised;
        if(!fnv_x87_calculate(FNV_X87_ADD,(uint16_t)(0x037f|(rc<<10)),a,b,&r,&raised) ||
            r.sign_exponent!=0x3fff || r.significand!=a.significand+(rc==2) || raised!=32)return 1;
    }
    return 0;
}
#endif

static void hardware(const unsigned char *a,const unsigned char *b,unsigned short cw,
                     unsigned op,unsigned char result[10],unsigned short *status) {
    unsigned short old;
    __asm__ volatile("fnstcw %0" : "=m"(old));
    __asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
#define BINARY(bytes) __asm__ volatile("fldt %2; fldt %3; " bytes "; fstpt %0; fnstsw %1" \
    : "=m"(*(unsigned char (*)[10])result),"=m"(*status) \
    : "m"(*(const unsigned char (*)[10])a),"m"(*(const unsigned char (*)[10])b) : "st","st(1)")
    switch(op) {
    case FNV_X87_ADD:BINARY(".byte 0xde,0xc1");break;
    case FNV_X87_SUB:BINARY(".byte 0xde,0xe9");break;
    case FNV_X87_MUL:BINARY(".byte 0xde,0xc9");break;
    case FNV_X87_DIV:BINARY(".byte 0xde,0xf9");break;
    default:__asm__ volatile("fldt %2; fsqrt; fstpt %0; fnstsw %1"
        : "=m"(*(unsigned char (*)[10])result),"=m"(*status)
        : "m"(*(const unsigned char (*)[10])a) : "st");break;
    }
#undef BINARY
    __asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
static void hardware_convert(const unsigned char *input,unsigned short cw,unsigned width,int load,
                             unsigned char *out,unsigned short *status) {
    unsigned short old;__asm__ volatile("fnstcw %0" : "=m"(old));
    __asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
#define CONVERT(in_type,out_type,instruction) __asm__ volatile(instruction "; fnstsw %1" \
    : "=m"(*(unsigned char (*)[out_type])out),"=m"(*status) \
    : "m"(*(const unsigned char (*)[in_type])input) : "st")
    if(load) {
        if(width==4) {CONVERT(4,10,"flds %2; fstpt %0");}
        else {CONVERT(8,10,"fldl %2; fstpt %0");}
    } else {
        if(width==4) {CONVERT(10,4,"fldt %2; fstps %0");}
        else {CONVERT(10,8,"fldt %2; fstpl %0");}
    }
#undef CONVERT
    __asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
static void hardware_sequence(const unsigned char *a,const unsigned char *b,unsigned short cw,
                              unsigned sequence,unsigned char result[10],unsigned short *status) {
    unsigned short old;__asm__ volatile("fnstcw %0" : "=m"(old));
    __asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
#define SEQUENCE(text) __asm__ volatile("fldt %2; " text "; fstpt %0; fnstsw %1" \
    : "=m"(*(unsigned char (*)[10])result),"=m"(*status) \
    : "m"(*(const unsigned char (*)[10])a),"m"(*(const unsigned char (*)[10])b) : "st","st(1)")
    switch(sequence) {
    case 0:SEQUENCE(".byte 0xd9,0xc0,0xde,0xc1");break;
    case 1:SEQUENCE("fldt %3; .byte 0xd9,0xc9,0xdd,0xd1,0xde,0xc1");break;
    case 2:SEQUENCE(".byte 0xdd,0xd8,0xd9,0xe8");break;
    default:SEQUENCE(".byte 0xdd,0xd8,0xd9,0xee");break;
    }
#undef SEQUENCE
    __asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
static int contexts(void) {
    struct fnv_x87_stack state,before,copy,other;
    unsigned char memory[10],saved[10];memset(memory,0xa5,10);memcpy(saved,memory,10);
    fnv_x87_stack_init(&state);memcpy(&before,&state,sizeof(state));
    if(fnv_x87_stack_store_pop(&state,memory)!=FNV_X87_STACK_EMPTY ||
       memcmp(memory,saved,10) || memcmp(&state,&before,sizeof(state)))return 1;
    unsigned char unnormal[10]={1,0,0,0,0,0,0,0,0xff,0x3f};
    if(fnv_x87_stack_load(&state,unnormal)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&state,&before,sizeof(state)))return 2;
    state.control=0x037e;memcpy(&before,&state,sizeof(state));
    if(fnv_x87_stack_constant(&state,1)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&state,&before,sizeof(state)))return 3;
    fnv_x87_stack_init(&state);memcpy(&before,&state,sizeof(state));
    if(fnv_x87_stack_duplicate(&state,8)!=FNV_X87_STACK_INVALID || memcmp(&state,&before,sizeof(state)))return 4;
    for(unsigned i=0;i<8;i++) {
        fnv_x87_pack(memory,(struct fnv_x87_extended){UINT64_C(0x8000000000000000)+i,0x3fff});
        if(fnv_x87_stack_load(&state,memory)!=FNV_X87_STACK_OK)return 5;
    }
    memcpy(&before,&state,sizeof(state));
    if(fnv_x87_stack_constant(&state,1)!=FNV_X87_STACK_FULL || memcmp(&state,&before,sizeof(state)))return 6;
    memcpy(&copy,&state,sizeof(state));fnv_x87_stack_init(&other);
    for(unsigned i=0;i<8;i++) {
        if(fnv_x87_stack_constant(&other,i%2)!=FNV_X87_STACK_OK || fnv_x87_stack_store_pop(&other,saved)!=FNV_X87_STACK_OK ||
           fnv_x87_stack_store_pop(&copy,memory)!=FNV_X87_STACK_OK)return 7;
        struct fnv_x87_extended value=fnv_x87_unpack(memory),constant=fnv_x87_unpack(saved);
        if(value.significand!=UINT64_C(0x8000000000000000)+7-i || value.sign_exponent!=0x3fff ||
           constant.significand!=(i%2 ? UINT64_C(0x8000000000000000):0) ||
           constant.sign_exponent!=(i%2 ? 0x3fff:0) || memcmp(&state,&before,sizeof(state)))return 8;
    }
    if(copy.top || copy.occupied || other.top || other.occupied)return 9;
    return 0;
}
int main(void) {
#ifdef _WIN32
    if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 8;
#endif
    if(contexts())return 26;
    fprintf(stderr,"PASS context copies, stack capacity and non-writing failure paths\n");
    struct fnv_x87_extended sentinel={123,456},zero={0,0};uint16_t raised=0xabcd;
    if(fnv_x87_calculate(FNV_X87_ADD,0x017f,zero,zero,&sentinel,&raised) ||
       fnv_x87_calculate((enum fnv_x87_operation)99,0x037f,zero,zero,&sentinel,&raised) ||
       fnv_x87_calculate(FNV_X87_ADD,0x037f,zero,zero,NULL,&raised) ||
       fnv_x87_calculate(FNV_X87_ADD,0x037f,zero,zero,&sentinel,NULL) ||
       sentinel.significand!=123 || sentinel.sign_exponent!=456 || raised!=0xabcd)return 9;
#ifdef _WIN32
    HANDLE workers[4];
    for(unsigned i=0;i<4;i++) {
        workers[i]=CreateThread(NULL,0,worker,(LPVOID)(uintptr_t)i,0,NULL);
        if(!workers[i])return 10;
    }
    if(WaitForMultipleObjects(4,workers,TRUE,INFINITE)!=WAIT_OBJECT_0)return 11;
    for(unsigned i=0;i<4;i++) {
        DWORD code;
        if(!GetExitCodeThread(workers[i],&code) || code || !CloseHandle(workers[i]))return 12;
    }
    fprintf(stderr,"PASS 40000 concurrent arithmetic calls with four rounding controls\n");
#endif
    unsigned char request[24];unsigned count=0;
    for (;;) {
        size_t n=fread(request,1,sizeof(request),stdin);
        if(!n)break;
        if(n!=sizeof(request))return 3;
        unsigned short cw=(unsigned short)(request[20]|request[21]<<8),hwstatus,flags;
        unsigned char actual[14],expected[124];struct fnv_x87_extended result;
        if(!fnv_x87_calculate((enum fnv_x87_operation)request[22],cw,
            fnv_x87_unpack(request),fnv_x87_unpack(request+10),&result,&flags))return 4;
        fnv_x87_pack(actual,result);
        hardware(request,request+10,cw,request[22],expected,&hwstatus);
        hwstatus&=0x3d; /* DE/C1/stack/environment not covered by this arithmetic core. */
        if(memcmp(actual,expected,10) || flags!=hwstatus) {
            fprintf(stderr,"Mismatch case %u op %u cw %04X flags %04X/%04X result ",count,request[22],cw,flags,hwstatus);
            for(unsigned i=0;i<10;i++)fprintf(stderr,"%02X",actual[i]);
            fprintf(stderr," expected ");for(unsigned i=0;i<10;i++)fprintf(stderr,"%02X",expected[i]);
            fprintf(stderr,"\n");return 5;
        }
        struct fnv_x87_stack stack;fnv_x87_stack_init(&stack);stack.control=cw;
        if(fnv_x87_stack_load(&stack,request)!=FNV_X87_STACK_OK)return 13;
        if(request[22]==FNV_X87_SQRT) {
            if(fnv_x87_stack_sqrt(&stack)!=FNV_X87_STACK_OK)return 14;
        } else {
            if(fnv_x87_stack_load(&stack,request+10)!=FNV_X87_STACK_OK ||
               fnv_x87_stack_binary_pop(&stack,(enum fnv_x87_operation)request[22])!=FNV_X87_STACK_OK)return 15;
        }
        if(fnv_x87_stack_store_pop(&stack,actual)!=FNV_X87_STACK_OK ||
           memcmp(actual,expected,10) || (stack.status&0x3d)!=hwstatus || stack.top || stack.occupied || stack.control!=cw)return 16;
        expected[10]=(unsigned char)hwstatus;expected[11]=(unsigned char)(hwstatus>>8);
        unsigned offset=12;
        for(unsigned conversion=0;conversion<6;conversion++) {
            unsigned width=conversion%2 ? 8 : 4,length=conversion>=2 ? 10 : width;
            unsigned char input[10];memcpy(input,request,10);
            if(conversion>=4)input[width-1]^=0x80;
            hardware_convert(input,cw,width,conversion>=2,expected+offset,&hwstatus);
            hwstatus&=0x3f;
            fnv_x87_stack_init(&stack);stack.control=cw;
            memset(actual,0xa5,sizeof(actual));
            if(conversion>=2) {
                if(fnv_x87_stack_load_binary(&stack,input,width)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_store_pop(&stack,actual)!=FNV_X87_STACK_OK)return 17;
            } else {
                if(fnv_x87_stack_load(&stack,request)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_store_binary(&stack,actual,width,1)!=FNV_X87_STACK_OK)return 18;
            }
            if(memcmp(actual,expected+offset,length) || (stack.status&0x3f)!=hwstatus || stack.top || stack.occupied) {
                fprintf(stderr,"Conversion mismatch case %u conversion %u flags %04X/%04X\n",count,conversion,stack.status&0x3f,hwstatus);return 19;
            }
            expected[offset+length]=(unsigned char)hwstatus;expected[offset+length+1]=(unsigned char)(hwstatus>>8);
            for(unsigned i=length;i<sizeof(actual);i++)if(actual[i]!=0xa5)return 20;
            offset+=length+2;
        }
        for(unsigned sequence=0;sequence<4;sequence++) {
            hardware_sequence(request,request+10,cw,sequence,expected+offset,&hwstatus);hwstatus&=0x3f;
            fnv_x87_stack_init(&stack);stack.control=cw;
            if(fnv_x87_stack_load(&stack,request)!=FNV_X87_STACK_OK)return 21;
            if(sequence==0) {
                if(fnv_x87_stack_duplicate(&stack,0)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_binary_pop(&stack,FNV_X87_ADD)!=FNV_X87_STACK_OK)return 22;
            } else if(sequence==1) {
                if(fnv_x87_stack_load(&stack,request+10)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_exchange(&stack,1)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_store_register(&stack,1,0)!=FNV_X87_STACK_OK ||
                   fnv_x87_stack_binary_pop(&stack,FNV_X87_ADD)!=FNV_X87_STACK_OK)return 23;
            } else if(fnv_x87_stack_store_register(&stack,0,1)!=FNV_X87_STACK_OK ||
                      fnv_x87_stack_constant(&stack,sequence==2)!=FNV_X87_STACK_OK)return 24;
            if(fnv_x87_stack_store_pop(&stack,actual)!=FNV_X87_STACK_OK || memcmp(actual,expected+offset,10) ||
               (stack.status&0x3f)!=hwstatus || stack.top || stack.occupied)return 25;
            expected[offset+10]=(unsigned char)hwstatus;expected[offset+11]=(unsigned char)(hwstatus>>8);offset+=12;
        }
        if(offset!=124 || fwrite(expected,1,124,stdout)!=124)return 6;
        count++;
    }
    if(ferror(stdin))return 7;
    fprintf(stderr,"PASS %u native x87 differential arithmetic cases\n",count);
    return 0;
}
