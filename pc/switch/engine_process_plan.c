#include "engine_process_plan.h"
#include "guest_cpuid.h"
int fnv_engine_process_plan_create(uint32_t base,uint32_t image_bytes,
    unsigned char *arena,size_t bytes,struct fnv_vm_permissions *permissions,
    struct fnv_engine_process_plan *out) {
    if(!out || !arena || ((uintptr_t)arena&4095) || !permissions || !permissions->set || image_bytes<=4096)return 0;
    struct fnv_engine_process_plan p={0};
    if(!fnv_engine_layout_create(base,image_bytes,&p.layout) || bytes<p.layout.arena_bytes ||
       (uintptr_t)arena>UINTPTR_MAX-p.layout.arena_bytes)return 0;
    const struct fnv_engine_region *r=p.layout.regions;
    p.vm=(struct fnv_vm_config){arena+r[FNV_ENGINE_VM].offset,base+r[FNV_ENGINE_VM].offset,
        r[FNV_ENGINE_VM].bytes,p.layout.commit_limit,permissions,fnv_vm_eager_apply};
    struct fnv_windows_config *c=&p.process;
    c->cpu.cpuid=fnv_guest_cpuid_limits;c->cpu.memory=arena;
    c->cpu.guest_base=base;c->cpu.memory_bytes=p.layout.arena_bytes;
    c->cpu.code_low=base+4096;c->cpu.code_high=base+image_bytes;
    c->image_bytes=image_bytes;c->process_id=0x1711;c->peb_address=base+r[FNV_ENGINE_PEB].offset;
    c->heap_address=base+r[FNV_ENGINE_HEAP].offset;c->heap_bytes=r[FNV_ENGINE_HEAP].bytes;
    c->heap_capacity=65536;c->private_heap_reserve=16u<<20;c->private_heap_limit=8;
    c->parameters_address=base+r[FNV_ENGINE_PARAMETERS].offset;c->parameters_bytes=r[FNV_ENGINE_PARAMETERS].bytes;
    c->command_line="FalloutNV.exe";c->image_path="FalloutNV.exe";
    c->kernel_handle_capacity=1024;c->critical_section_capacity=4096;
    c->file_root="sdmc:/switch/FNV-nx";c->file_handle_capacity=128;
    c->worker_address=base+r[FNV_ENGINE_WORKERS].offset;c->worker_bytes=r[FNV_ENGINE_WORKERS].bytes;
    c->worker_capacity=p.layout.worker_capacity;c->worker_stack_bytes=p.layout.worker_stack_bytes;
    c->worker_tls_bytes=p.layout.worker_tls_bytes;c->worker_host_stack_bytes=p.layout.worker_host_stack_bytes;
    struct fnv_windows_thread_config *t=&p.main_thread;
    t->thread_id=22;t->teb_address=base+r[FNV_ENGINE_MAIN_THREAD].offset;
    t->tls_expansion_address=t->teb_address+4096;t->static_tls_vector=t->teb_address+8192;
    t->static_tls_vector_bytes=4096;t->stack_low=t->teb_address+0x4000;
    t->stack_high=t->teb_address+0x24000;t->static_tls_data=t->teb_address+0x30000;
    t->static_tls_data_bytes=0x10000;
    *out=p;return 1;
}
