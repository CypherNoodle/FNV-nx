#include <windows.h>
#include "windows_thread.h"
#include "steam_callbacks.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
static unsigned char *arena;
static void write32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t read32(uint32_t at){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)arena[at-BASE+i]<<(8*i);return v;}
static enum fnv_cpu_result call(unsigned api,uint32_t object,uint32_t id){
    struct fnv_cpu_state s,after;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    s.gpr[4]=BASE+0x1bf00;s.gpr[0]=0xdeadbeef;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    write32(s.gpr[4],0x401500);write32(s.gpr[4]+4,object);write32(s.gpr[4]+8,id);
    write32(BASE+0x10034,0x12345678);
    struct fnv_cpu_fault fault;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[4]==s.gpr[4]+4+(api==2?0:api?4:8) && after.gpr[0]==(api==2?0:s.gpr[0]));
        assert(read32(BASE+0x10034)==0x12345678);
        for(unsigned i=3;i<8;i++)if(i!=4)assert(after.gpr[i]==s.gpr[i]);}
    else {fprintf(stderr,"callback fault api=%u object=%x id=%u: %s\n",api,object,id,fault.text);
        assert(r==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);}
    return r;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_steam_callbacks *service,*other=(void *)1;
    assert(fnv_steam_callbacks_create(process,&service)==FNV_CPU_OK);
    assert(fnv_steam_callbacks_create(process,&other)==FNV_CPU_BUSY && !other);
    size_t n,bound;const struct fnv_cpu_export *e=fnv_steam_callbacks_exports(&n);assert(n==3);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,e,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;
    t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(fnv_steam_callbacks_destroy(service)==FNV_CPU_BUSY);
    uint32_t object=BASE+0x80000,count;unsigned char *p=arena+0x80000;
    memset(p,0xa5,128*16);for(unsigned i=0;i<128;i++)p[i*16+4]=0;
    if(argc==3){
        if(!strcmp(argv[2],"--strict-null"))object=0;
        else if(!strcmp(argv[2],"--strict-range"))object=BASE+BYTES-11;
        else if(!strcmp(argv[2],"--strict-flags"))p[4]=4;
        else if(!strcmp(argv[2],"--strict-unowned"))p[4]=1;
        else if(!strcmp(argv[2],"--strict-rebind"))assert(call(0,object,1102)==FNV_CPU_OK);
        else if(!strcmp(argv[2],"--strict-capacity")){
            for(unsigned i=0;i<128;i++)assert(call(0,object+i*16,1101)==FNV_CPU_OK);
            object+=128*16;arena[object-BASE+4]=0;
        }else assert(0);
        assert(call(0,object,1101)==FNV_CPU_GUEST_FAULT);
        puts("strict PASS");
    }else{
        for(unsigned i=0;i<1000;i++)assert(call(2,0,0)==FNV_CPU_OK);
        for(unsigned round=0;round<20;round++){
            for(unsigned i=0;i<128;i++){
                p[i*16+4]=(unsigned char)((i&1)*2);
                assert(call(0,object+i*16,1101+i)==FNV_CPU_OK);
                assert(call(0,object+i*16,1101+i)==FNV_CPU_OK);
                assert(p[i*16+4]==(unsigned char)(1+(i&1)*2));
                assert(read32(object+i*16+8)==1101+i);
                for(unsigned k=0;k<16;k++)if(k!=4 && (k<8 || k>=12))assert(p[i*16+k]==0xa5);
            }
            assert(fnv_steam_callbacks_count(service,&count)==FNV_CPU_OK && count==128);
            unsigned char saved[128*16];memcpy(saved,p,sizeof(saved));
            for(unsigned i=0;i<50;i++)assert(call(2,0,0)==FNV_CPU_OK);
            assert(!memcmp(saved,p,sizeof(saved)));
            assert(fnv_steam_callbacks_count(service,&count)==FNV_CPU_OK && count==128);
            for(unsigned i=0;i<128;i++){
                assert(call(1,object+i*16,0)==FNV_CPU_OK);
                assert(call(1,object+i*16,0)==FNV_CPU_OK);
                assert(p[i*16+4]==(unsigned char)((i&1)*2));
            }
            assert(fnv_steam_callbacks_count(service,&count)==FNV_CPU_OK && count==0);
        }
        puts("{\"status\":\"PASS\",\"translated_calls\":12240,\"no_client_init_checks\":2000,\"cdecl_cleanup_checked\":true,\"engine_started\":false}");
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);
    assert(fnv_steam_callbacks_destroy(service)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));return 0;
}
