#include <windows.h>
#include "windows_strings.h"
#include "windows_thread.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x400000u
#define SOURCE (BASE+0x50000)
#define DEST (BASE+0x70000)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){memcpy(arena+at-BASE,&v,4);}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,int negative){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);put(s.gpr[4]+4,a);put(s.gpr[4]+8,b);struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT&&fault.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    assert(r==FNV_CPU_OK&&fnv_cpu_get_state(&after)==FNV_CPU_OK&&after.gpr[4]==s.gpr[4]+4+(api==0||api==3?8:api==1?0:4));return after.gpr[0];
}
static uint32_t permissions(void *ctx,void *at,uint32_t bytes,uint32_t p){(void)ctx;(void)at;(void)bytes;(void)p;return 0;}
int main(int argc,char **argv){
    assert(argc==2||argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_vm_permissions permission={NULL,permissions};struct fnv_vm_config v={arena+0x100000,BASE+0x100000,0x300000,0x300000,&permission,fnv_vm_eager_apply};struct fnv_guest_memory *vm;assert(!fnv_vm_create(&v,&vm).error);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;c.memory=vm;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_strings_exports(&n);assert(n==2);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK&&bound==4);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){
        if(!strcmp(argv[2],"--strict-overlap")){strcpy((char*)arena+SOURCE-BASE,"overlap");call(0,SOURCE+1,SOURCE,1);}
        else {struct fnv_vm_result large=fnv_vm_allocate(vm,0,0x110000,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!large.error);memset(arena+large.value-BASE,'x',0x110000);call(0,DEST,large.value,1);}
        goto finish;
    }
    unsigned char text[4096],native[4096];unsigned seed=0x88712;
    for(unsigned i=0;i<3000;i++){
        seed=seed*1664525+1013904223;unsigned length=seed%2048,offset=i%31;
        for(unsigned j=0;j<length;j++)text[j]=(unsigned char)(1+(i+j*79)%255);text[length]=0;
        memcpy(arena+SOURCE-BASE+offset,text,length+1);memset(arena+DEST-BASE-1,0xa5,4096);memset(native,0xa5,sizeof(native));
        SetLastError(0x4234);assert(lstrcpyA((char*)native+1,(char*)text)==(char*)native+1);DWORD error=GetLastError();call(2,0x4234,0,0);
        assert(call(0,DEST,SOURCE+offset,0)==DEST&&call(1,0,0,0)==error&&!memcmp(arena+DEST-BASE-1,native,4096));
        memset(arena+DEST-BASE-1,0xa5,4096);memset(native,0xa5,sizeof(native));strcpy((char*)native+1,"DATA/");strcpy((char*)arena+DEST-BASE,"DATA/");
        SetLastError(0x4234);assert(lstrcatA((char*)native+1,(char*)text)==(char*)native+1);error=GetLastError();call(2,0x4234,0,0);
        assert(call(3,DEST,SOURCE+offset,0)==DEST&&call(1,0,0,0)==error&&!memcmp(arena+DEST-BASE-1,native,4096));
    }
    char marker[8]="abc";SetLastError(0x4234);assert(!lstrcpyA(NULL,"x"));DWORD error=GetLastError();call(2,0x4234,0,0);assert(!call(0,0,SOURCE,0)&&call(1,0,0,0)==error);
    SetLastError(0x4234);assert(!lstrcpyA(marker,NULL));error=GetLastError();call(2,0x4234,0,0);assert(!call(0,DEST,0,0)&&call(1,0,0,0)==error);
    struct fnv_vm_result block=fnv_vm_allocate(vm,0,4096,FNV_VM_RESERVE|FNV_VM_COMMIT,FNV_VM_READWRITE);assert(!block.error);arena[block.value-BASE+4095]=0;
    assert(call(0,DEST,block.value+4095,0)==DEST&&arena[DEST-BASE]==0);
    arena[block.value-BASE+4095]='X';memset(arena+DEST-BASE,0x6d,32);assert(!call(0,DEST,block.value+4095,0));for(unsigned i=0;i<32;i++)assert(arena[DEST-BASE+i]==0x6d);
    uint32_t old;assert(!fnv_vm_protect(vm,block.value,4096,FNV_VM_READONLY,&old).error);assert(!call(0,block.value,SOURCE,0));
finish:
    assert(fnv_windows_thread_detach()==FNV_CPU_OK&&fnv_windows_destroy(process)==FNV_CPU_OK&&!fnv_vm_destroy(vm).error&&VirtualFree(arena,0,MEM_RELEASE));
    if(argc==3){puts("strict PASS");return 0;}
    puts("{\"status\":\"PASS\",\"native_windows_copies\":3000,\"native_windows_appends\":3000,\"null_and_page_boundaries\":true,\"engine_started\":false}");return 0;
}
