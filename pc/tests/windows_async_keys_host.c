#include <windows.h>
#include "windows_thread.h"
#include "windows_user32.h"
#include "windows_user32_input.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define MSG_AT (BASE+0x9000u)
static unsigned char *arena;
static void put(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static enum fnv_cpu_result invoke(unsigned api,const uint32_t args[5],uint32_t *answer,struct fnv_cpu_fault *fault){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x1bf00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put(s.gpr[4],0x401500);for(unsigned i=0;i<5;i++)put(s.gpr[4]+4+i*4,args[i]);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x1bf04+(api==1?0:4));*answer=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t at,uint32_t hwnd,uint32_t low,uint32_t high,uint32_t flags){
    uint32_t a[]={at,hwnd,low,high,flags},value=0;struct fnv_cpu_fault f;
    assert(invoke(api,a,&value,&f)==FNV_CPU_OK);return value;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
    struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_windows_user32 *desktop;assert(fnv_windows_user32_create(process,&desktop)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_user32_exports(&n);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x10000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x14000;t.stack_high=BASE+0x1c000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){uint32_t args[]={65,0,0,0,0},answer=0;struct fnv_cpu_fault f;
        assert(invoke(0,args,&answer,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED && strstr(f.text,"snapshot unavailable"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    unsigned checks=0;unsigned char keys[256]={0};
    assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
    for(unsigned round=0;round<500;round++){
        unsigned key=0x41+round%26;
        keys[key]=128;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
        call(2,0x1234,0,0,0,0);assert(call(0,key,0,0,0,0)==0x8001 && call(1,0,0,0,0,0)==0x1234);
        assert(call(0,key,0,0,0,0)==0x8000);
        assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK && call(0,key,0,0,0,0)==0x8000);
        keys[key]=0;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK && !call(0,key,0,0,0,0));
        keys[key]=128;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
        keys[key]=0;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK && call(0,key,0,0,0,0)==1 && !call(0,key,0,0,0,0));
        checks+=6;
    }
    for(unsigned side=0;side<6;side++){
        memset(keys,0,256);keys[0xa0+side]=128;
        assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
        assert(call(0,0xa0+side,0,0,0,0)==0x8001 && call(0,0x10+side/2,0,0,0,0)==0x8001);
        assert(fnv_windows_user32_update_keys(desktop,keys,0)==FNV_CPU_OK && !call(0,0xa0+side,0,0,0,0));checks+=3;
    }
    /* Physical mouse buttons are not affected by logical swap-button setting. */
    memset(keys,0,256);keys[1]=128;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
    assert(call(0,1,0,0,0,0)==0x8001 && !call(0,2,0,0,0,0));
    keys[3]=1;assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_INVALID && call(0,1,0,0,0,0)==0x8000);
    assert(fnv_windows_user32_update_keys(desktop,keys,2)==FNV_CPU_INVALID);
    memset(keys,0,256);assert(fnv_windows_user32_update_keys(desktop,keys,1)==FNV_CPU_OK);
    struct fnv_di_snapshot di={0};di.keys[0x1e]=128;di.keys[0x2a]=128;di.buttons[0]=128;
    assert(fnv_user32_update_di_snapshot(desktop,&di,1)==FNV_CPU_OK);
    assert(call(0,0x41,0,0,0,0)==0x8001 && !call(0,0x1e,0,0,0,0));
    assert(call(0,0x10,0,0,0,0)==0x8001 && call(0,1,0,0,0,0)==0x8001);
    di.keys[0xff]=128;assert(fnv_user32_update_di_snapshot(desktop,&di,1)==FNV_CPU_INVALID && call(0,0x41,0,0,0,0)==0x8000);
    di.keys[0xff]=0;assert(fnv_user32_update_di_snapshot(desktop,&di,0)==FNV_CPU_OK && !call(0,0x41,0,0,0,0));
    for(unsigned i=0;i<32;i++){
        int invalid=i&1?-1-(int)i:256+(int)i;SetLastError(0x2345);SHORT expected=GetAsyncKeyState(invalid);DWORD error=GetLastError();
        fprintf(stderr,"INVALID_KEY %d native=%04x error=%lu\n",invalid,(unsigned)(uint16_t)expected,(unsigned long)error);call(2,0x2345,0,0,0,0);assert((uint16_t)call(0,(uint32_t)invalid,0,0,0,0)==(uint16_t)expected && call(1,0,0,0,0,0)==0x2345);
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_user32_destroy(desktop)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"controlled_snapshot_checks\":%u,\"windows_invalid_key_comparisons\":32,\"input_hardware_tested\":false,\"native_engine_started\":false}\n",checks);return 0;
}
