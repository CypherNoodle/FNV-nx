/* Original synthetic dispatch/fault test; no translated game functions. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu_runtime.h"
#include "fnv_sdk_runtime.h"
#include "x87_transcendental_candidate.h"
#include "x87_trig_candidate.h"
#define BASE 0x400000u
#define BYTES 0x10000u
static unsigned returned;
static void math_entry(void){fnv_x87_transcendental_candidate(&fnv_x87_state,FNV_X87_CANDIDATE_F2XM1);returned++;}
static void trig_entry(void){fnv_x87_trig_candidate(&fnv_x87_state,FNV_X87_TRIG_FSIN);returned++;}
recomp_func_t recomp_lookup(uint32_t va){return va==0x401000?math_entry:va==0x401010?trig_entry:NULL;}
int main(int argc,char **argv){
    assert(argc==2);unsigned char *arena=calloc(1,BYTES);assert(arena);
    for(unsigned mode=0;mode<2;mode++){
        struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
        struct fnv_cpu_config c={.memory=arena,.guest_base=BASE,.memory_bytes=BYTES,.code_low=BASE+0x1000,.code_high=BASE+0x2000};
        struct fnv_cpu_runtime *cpu;assert(fnv_cpu_create(&c,&cpu)==FNV_CPU_OK);
        struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
        assert(fnv_cpu_bind_imports(cpu,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && !count);
        struct fnv_cpu_state state;fnv_cpu_state_init(&state,BASE+BYTES-64,0);
        state.x87_extended.occupied=1;state.x87_extended.registers[0]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
        state.x87_environment=(struct fnv_x87_environment){0x12345678,0x23456789,0x23,0x2b,0x345};
        assert(fnv_cpu_attach(cpu,&state)==FNV_CPU_OK);
        struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401000+mode*16,&fault)==FNV_CPU_GUEST_FAULT);
        assert(fault.kind==FNV_CPU_UNIMPLEMENTED && strstr(fault.text,"Target ARM64 x87") && !returned);
        assert(!memcmp(&fault.state.x87_extended,&state.x87_extended,sizeof state.x87_extended));
        assert(!memcmp(&fault.state.x87_environment,&state.x87_environment,sizeof state.x87_environment));
        assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    }
    free(arena);puts("{\"status\":\"PASS\",\"explicit_math_faults\":2,\"silent_returns\":0,\"x87_state_preserved\":true}");return 0;
}
