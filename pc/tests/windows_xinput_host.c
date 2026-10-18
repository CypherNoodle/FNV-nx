#include <windows.h>
#include "xinput.h"
#include "pcm_queue.h"
#include "host_thread.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define ARENA 0x100000u
#define OUTPUT 0x409000u
static unsigned char *arena;
static uint32_t calls,user_seen;static uint16_t left_seen,right_seen;static struct fnv_xinput *input;
static uint32_t rumble(void *c,uint32_t user,uint16_t left,uint16_t right){
    assert(c==&calls);calls++;user_seen=user;left_seen=left;right_seen=right;return 0;
}
static void put32(uint32_t at,uint32_t v){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(v>>(8*i));}
static uint32_t get32(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t get16(uint32_t at){unsigned char *p=arena+at-BASE;return p[0]|((uint16_t)p[1]<<8);}
static enum fnv_cpu_result invoke(unsigned api,uint32_t user,uint32_t at,uint32_t *value){
    struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=0x41bf00;
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);put32(s.gpr[4],0);put32(s.gpr[4]+4,user);put32(s.gpr[4]+8,at);
    struct fnv_cpu_fault f;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,&f);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==0x41bf0c);*value=s.gpr[0];}
    return r;
}
static uint32_t call(unsigned api,uint32_t user,uint32_t at){uint32_t v;assert(invoke(api,user,at,&v)==FNV_CPU_OK);assert(get32(0x410034)==0x123456);return v;}
struct stream {struct fnv_pcm_queue *queue;atomic_uint done;};
static void producer(void *context){
    struct stream *s=context;int16_t block[16];
    for(unsigned i=0;i<10000;i++){
        for(unsigned k=0;k<16;k++)block[k]=(int16_t)((i*16+k)%30000+1);
        enum fnv_pcm_result r;while((r=fnv_pcm_write(s->queue,block,8))==FNV_PCM_FULL)fnv_host_thread_sleep(0);
        assert(r==FNV_PCM_OK);
    }atomic_store(&s->done,1);
}
static void consumer(void *context){
    struct stream *s=context;int16_t block[26];unsigned samples=0;
    while(samples<160000){unsigned count=fnv_pcm_read(s->queue,block,13);
        for(unsigned k=0;k<count*2;k++)assert(block[k]==(int16_t)((samples+k)%30000+1));
        for(unsigned k=count*2;k<26;k++)assert(block[k]==0);
        samples+=count*2;if(!count)fnv_host_thread_sleep(0);
    }
}
static void audio_test(void){
    uint64_t samples=0;
    for(unsigned capacity=1;capacity<140;capacity+=7){
        struct fnv_pcm_queue *q;assert(fnv_pcm_create(capacity,&q)==FNV_PCM_OK);int16_t reference[280],in[280],out[280];unsigned count=0,seed=12345;
        for(unsigned n=0;n<3000;n++){
            seed=seed*1664525u+1013904223u;unsigned frames=(seed>>12)%(capacity+1);
            if(seed&1){for(unsigned k=0;k<frames*2;k++)in[k]=(int16_t)(seed+k);
                enum fnv_pcm_result r=fnv_pcm_write(q,in,frames);
                if(frames<=capacity-count){assert(r==FNV_PCM_OK);memcpy(reference+count*2,in,frames*4);count+=frames;}
                else assert(r==FNV_PCM_FULL);
            }else{unsigned want=frames<count?frames:count;assert(fnv_pcm_read(q,out,frames)==want);
                assert(!memcmp(out,reference,want*4));for(unsigned k=want*2;k<frames*2;k++)assert(out[k]==0);
                memmove(reference,reference+want*2,(count-want)*4);count-=want;samples+=want;
            }
            struct fnv_pcm_stats stats;assert(fnv_pcm_stats(q,&stats)==FNV_PCM_OK && stats.queued==count && stats.capacity==capacity);
        }fnv_pcm_destroy(q);
    }
    assert(samples>100000);
    struct stream s;atomic_init(&s.done,0);assert(fnv_pcm_create(128,&s.queue)==FNV_PCM_OK);
    fnv_host_thread a,b;assert(fnv_host_thread_create(&a,producer,&s,65536) && fnv_host_thread_create(&b,consumer,&s,65536));
    assert(fnv_host_thread_join(&a) && fnv_host_thread_join(&b) && atomic_load(&s.done));struct fnv_pcm_stats stats;
    assert(fnv_pcm_stats(s.queue,&stats)==FNV_PCM_OK && stats.written==80000 && stats.read==80000 && !stats.queued);fnv_pcm_destroy(s.queue);
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=0x401000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=0x408000;c.process_id=11;struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_xinput_config config={&calls,rumble};assert(fnv_xinput_create(process,&config,&input)==FNV_CPU_OK);
    size_t count;const struct fnv_cpu_export *exports=fnv_xinput_exports(&count);assert(count==4);
    struct fnv_native_import imports[4];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,4,exports,count,&imported)==FNV_NATIVE_OK && imported==2);
    assert(imports[0].ordinal==2 && imports[1].ordinal==3);
    struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=0x410000;t.tls_expansion_address=0x411000;t.stack_low=0x414000;t.stack_high=0x41c000;
    fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);put32(t.teb_address+0x34,0x123456);
    assert(fnv_xinput_destroy(input)==FNV_CPU_BUSY && fnv_windows_destroy(process)==FNV_CPU_BUSY);
    struct fnv_gamepad g={0};assert(fnv_xinput_update(input,0,1,&g)==FNV_CPU_OK);
    if(argc==3){uint32_t value;unsigned api=!strcmp(argv[2],"--bad-vibration");assert(invoke(api,0,UINT32_MAX,&value)==FNV_CPU_GUEST_FAULT);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_xinput_destroy(input)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    assert(call(0,0,OUTPUT)==0 && get32(OUTPUT)==1);uint32_t packet=1;
    for(unsigned i=0;i<5000;i++){
        g=(struct fnv_gamepad){(uint16_t)i,(uint8_t)i,(uint8_t)(i>>3),(int16_t)(i*13),(int16_t)(-32768+(int)i),(int16_t)(32767-(int)i),(int16_t)(-((int)i*7))};
        assert(fnv_xinput_update(input,0,1,&g)==FNV_CPU_OK);packet++;
        memset(arena+OUTPUT-BASE-4,0xa5,24);assert(call(0,0,OUTPUT)==0 && get32(OUTPUT)==packet);
        assert(get16(OUTPUT+4)==g.buttons && arena[OUTPUT-BASE+6]==g.left_trigger && arena[OUTPUT-BASE+7]==g.right_trigger);
        assert((int16_t)get16(OUTPUT+8)==g.lx && (int16_t)get16(OUTPUT+10)==g.ly && (int16_t)get16(OUTPUT+12)==g.rx && (int16_t)get16(OUTPUT+14)==g.ry);
        assert(get32(OUTPUT-4)==0xa5a5a5a5 && get32(OUTPUT+16)==0xa5a5a5a5);
        assert(fnv_xinput_update(input,0,1,&g)==FNV_CPU_OK && call(0,0,OUTPUT)==0 && get32(OUTPUT)==packet);
        put32(OUTPUT,(uint32_t)i|((uint32_t)(65535-i)<<16));assert(call(1,0,OUTPUT)==0);
        assert(calls==i+1 && user_seen==0 && left_seen==i && right_seen==65535-i);
    }
    memset(arena+OUTPUT-BASE,0x9a,16);assert(call(0,1,OUTPUT)==1167 && get32(OUTPUT)==0x9a9a9a9a);
    assert(call(0,4,OUTPUT)==160 && call(1,99,OUTPUT)==160 && call(1,1,0)==1167);
    assert(fnv_xinput_update(input,0,0,NULL)==FNV_CPU_OK && call(0,0,0)==1167);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_xinput_destroy(input)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
    assert(VirtualFree(arena,0,MEM_RELEASE));audio_test();
    puts("{\"status\":\"PASS\",\"translated_input_states\":5000,\"rumble_requests\":5000,\"pcm_model_steps\":60000,\"concurrent_pcm_frames\":80000,\"native_engine_started\":false}");return 0;
}
