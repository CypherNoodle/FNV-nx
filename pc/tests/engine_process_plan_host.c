#include "engine_process_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint32_t forbid(void *context,void *memory,uint32_t bytes,uint32_t permission) {
    (void)context;(void)memory;(void)bytes;(void)permission;abort();
}
int main(void) {
    /* Pure descriptor construction: no access to or clearing of this backing.
     * The test reserves an address range but never executes the resulting plan. */
    unsigned char *allocation=malloc((808u<<20)+4095);assert(allocation);
    unsigned char *arena=(void *)(((uintptr_t)allocation+4095)&~(uintptr_t)4095);
    struct fnv_vm_permissions permissions={NULL,forbid};
    struct fnv_engine_process_plan p,sentinel;
    memset(&sentinel,0xa5,sizeof(sentinel));p=sentinel;
    assert(!fnv_engine_process_plan_create(0x400000,0x1080000,arena,64u<<20,&permissions,&p));
    assert(!memcmp(&p,&sentinel,sizeof(p)));
    assert(!fnv_engine_process_plan_create(0x400000,0x1080000,arena+1,808u<<20,&permissions,&p));
    assert(!fnv_engine_process_plan_create(0x400000,0x1080000,arena,808u<<20,NULL,&p));
    assert(fnv_engine_process_plan_create(0x400000,0x1080000,arena,808u<<20,&permissions,&p));
    assert(!p.process.memory && !p.main_thread.state.gpr[4]); /* No premature attach. */
    assert(p.vm.backing==arena+(48u<<20) && p.vm.bytes==760u<<20);
    assert(p.vm.base==p.process.cpu.guest_base+(48u<<20));
    assert(p.vm.context==&permissions && p.vm.apply==fnv_vm_eager_apply);
    assert(p.process.worker_capacity==32 && p.process.worker_bytes==0x660000);
    assert(p.process.worker_address+p.process.worker_bytes<=p.vm.base);
    assert(p.process.cpu.memory_bytes==808u<<20 && p.process.cpu.memory==arena);
    assert(p.main_thread.stack_high-p.main_thread.stack_low==0x20000);
    assert(p.main_thread.static_tls_data+p.main_thread.static_tls_data_bytes==
           p.main_thread.teb_address+0x40000);
    assert(!strcmp(p.process.file_root,"sdmc:/switch/FNV-nx"));
    assert(!strcmp(p.process.command_line,"FalloutNV.exe"));
    uint32_t cpuid[4];assert(p.process.cpu.cpuid(NULL,0x80000000,0,cpuid));
    assert(cpuid[0]==0x80000000 && !cpuid[1] && !cpuid[2] && !cpuid[3]);
    free(allocation);puts("PASS engine process plan: VM, 32 workers, main TLS/stack, SD root, no execution");return 0;
}
