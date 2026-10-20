/* Original synthetic CPU ABI tests: no game inputs. */
#undef NDEBUG
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#include "cpu_runtime.h"
#include "fnv_sdk_runtime.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef FNV_CPU_EXTENDED_X87
#error Extended CPU build required
#endif
#define BASE 0x400000u
#define BYTES 0x100000u
#define PUSH 0x401500u
#define POP 0x401580u
#define NEST 0x401600u
#define CHILD 0x401680u
#define FAULT 0x401700u
static struct fnv_cpu_runtime *runtime;
static unsigned char *arena;
static const char *fixture;
static atomic_uint ready,start;
static void yield_host(void){
#ifdef _WIN32
SwitchToThread();
#else
sched_yield();
#endif
}
static void put32(uint32_t at,uint32_t value){for(unsigned i=0;i<4;i++)arena[at-BASE+i]=(unsigned char)(value>>(8*i));}
static void session(void){
struct fnv_native_image image;assert(fnv_native_load_image(fixture,arena,BYTES,&image)==FNV_NATIVE_OK);
struct fnv_cpu_config config={.memory=arena,.guest_base=BASE,.memory_bytes=BYTES,.code_low=PUSH,.code_high=0x401800};
assert(fnv_cpu_create(&config,&runtime)==FNV_CPU_OK);
struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
/* This fixture has no imports; bind the empty catalog normally. */
assert(fnv_cpu_bind_imports(runtime,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && !count);
}
static struct fnv_cpu_state initial(unsigned id){
struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+BYTES-0x1000-id*0x1000,0);
s.gpr[0]=BASE+0x6000+id*0x200;put32(s.gpr[4],0xfffffff0);
s.x87_extended.top=(uint8_t)(id+1);s.x87_extended.status=(uint16_t)((id+1)<<11);
s.x87_extended.occupied=(uint8_t)(1u<<(id+1));
for(unsigned i=0;i<8;i++){s.x87_extended.registers[i].significand=UINT64_C(0x8000000000000001)+100*id+i;s.x87_extended.registers[i].sign_exponent=(uint16_t)(0x3fff+i);}
s.x87_environment=(struct fnv_x87_environment){0x12340000+id,0x34560000+id,0x23,0x2b,0x345};
unsigned char *input=arena+s.gpr[0]-BASE;struct fnv_x87_extended value={UINT64_C(0xc123456789abc001)+id,0x4000};fnv_x87_pack(input,value);
return s;
}
static void check_roundtrip(const struct fnv_cpu_state *s){struct fnv_cpu_state got;assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);assert(!memcmp(&got,s,sizeof(got)));}
#ifdef _WIN32
static DWORD WINAPI worker(void *arg){
#else
static void *worker(void *arg){
#endif
unsigned id=(unsigned)(uintptr_t)arg;struct fnv_cpu_state s=initial(id),got;
assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);atomic_fetch_add(&ready,1);
while(!atomic_load(&start))yield_host();
for(unsigned n=0;n<1000;n++){
assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);check_roundtrip(&s);
assert(fnv_cpu_execute(PUSH,NULL)==FNV_CPU_OK);yield_host();assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);
unsigned top=(s.x87_extended.top+7u)&7u;assert(got.x87_extended.top==top && got.x87_extended.occupied==(s.x87_extended.occupied|(1u<<top)));
assert(got.x87_extended.registers[top].significand==UINT64_C(0xc123456789abc001)+id && got.x87_extended.registers[top].sign_exponent==0x4000);
assert(got.x87_environment.instruction_pointer==PUSH && got.x87_environment.data_pointer==s.gpr[0]);
got.gpr[0]=s.gpr[0]+0x100;got.gpr[4]=s.gpr[4];assert(fnv_cpu_set_state(&got)==FNV_CPU_OK);
assert(fnv_cpu_execute(POP,NULL)==FNV_CPU_OK);assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);
assert(got.x87_extended.top==s.x87_extended.top && got.x87_extended.occupied==s.x87_extended.occupied);
assert(!memcmp(arena+s.gpr[0]-BASE,arena+s.gpr[0]+0x100-BASE,10));
assert(got.x87_environment.instruction_pointer==POP && got.x87_environment.data_pointer==s.gpr[0]+0x100);
if(n%100==0){assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);check_roundtrip(&s);}
}
assert(fnv_cpu_detach()==FNV_CPU_OK);return 0;
}
static void concurrent(void){session();atomic_store(&ready,0);atomic_store(&start,0);
#ifdef _WIN32
HANDLE threads[2];for(unsigned i=0;i<2;i++){threads[i]=CreateThread(NULL,0,worker,(void*)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
#else
pthread_t threads[2];for(unsigned i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,worker,(void*)(uintptr_t)(i+1)));
#endif
while(atomic_load(&ready)!=2)yield_host();atomic_store(&start,1);
#ifdef _WIN32
for(unsigned i=0;i<2;i++){assert(WaitForSingleObject(threads[i],15000)==WAIT_OBJECT_0);CloseHandle(threads[i]);}
#else
for(unsigned i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));
#endif
assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK);
}
static void invalid_contexts(void){session();struct fnv_cpu_state s=initial(1),bad;assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);
for(unsigned i=0;i<7;i++){bad=s;switch(i){case 0:bad.x87_state_abi=0;break;case 1:bad.x87_extended.top=8;break;case 2:bad.x87_extended.status^=0x800;break;case 3:bad.x87_environment.opcode=0x800;break;case 4:bad.x87_extended.control=0;break;case 5:bad.x87_extended.status|=0x80;break;case 6:bad.gpr[4]=UINT32_MAX;break;}
assert(fnv_cpu_set_state(&bad)==FNV_CPU_INVALID);check_roundtrip(&s);}
assert(fnv_cpu_detach()==FNV_CPU_OK);bad=s;bad.x87_state_abi=0;assert(fnv_cpu_attach(runtime,&bad)==FNV_CPU_INVALID);
assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);check_roundtrip(&s);assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK);
}
static void nested_and_fault(void){session();struct fnv_cpu_state s,got;fnv_cpu_state_init(&s,BASE+BYTES-0x1000,0);put32(s.gpr[4],0xfffffff0);
assert(fnv_cpu_attach(runtime,&s)==FNV_CPU_OK);assert(fnv_cpu_execute(NEST,NULL)==FNV_CPU_OK);assert(fnv_cpu_get_state(&got)==FNV_CPU_OK);
unsigned char expected[10],actual[10];uint16_t cw;__asm__ volatile("fnstcw %0":"=m"(cw));
__asm__ volatile("fninit;fld1;fldpi;faddp;fstpt %0;fldcw %1":"=m"(*(unsigned char(*)[10])expected):"m"(cw):"st","st(1)");
fnv_x87_pack(actual,got.x87_extended.registers[got.x87_extended.top]);assert(!memcmp(actual,expected,10));
assert(got.x87_extended.top==7 && got.x87_extended.occupied==128 && got.x87_environment.instruction_pointer==CHILD+2);
s=initial(1);assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);struct fnv_cpu_fault fault;
assert(fnv_cpu_execute(FAULT,&fault)==FNV_CPU_GUEST_FAULT);assert(fault.kind==FNV_CPU_UNIMPLEMENTED);
assert(fault.state.x87_state_abi==FNV_CPU_X87_STATE_ABI && fault.state.x87_extended.registers[(s.x87_extended.top+7)&7].significand==UINT64_C(0xc123456789abc002));
assert(fault.state.x87_environment.instruction_pointer==FAULT);assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_destroy(runtime)==FNV_CPU_OK);
}
int main(int argc,char **argv){assert(argc==2);fixture=argv[1];arena=malloc(BYTES);assert(arena);concurrent();invalid_contexts();nested_and_fault();free(arena);
printf("{\"status\":\"PASS\",\"host_threads\":2,\"push_pop_pairs\":2000,\"reattach_checks\":20,\"invalid_contexts\":8,\"nested_calls\":true,\"fault_snapshot\":true}\n");return 0;}
