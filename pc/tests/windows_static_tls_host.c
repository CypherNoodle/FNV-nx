#define main unused_workers_suite
#include "windows_workers_host.c"
#undef main
static DWORD WINAPI external_thread(void *context){
    (void)context;struct fnv_windows_thread_config t=main_config();t.thread_id=33;t.teb_address=BASE+0x30000;t.tls_expansion_address=BASE+0x31000;t.stack_low=BASE+0x34000;t.stack_high=BASE+0x3c000;
    t.static_tls_vector=BASE+0x23000;t.static_tls_vector_bytes=4;t.static_tls_data=BASE+0x21000;t.static_tls_data_bytes=4096;
    fnv_cpu_state_init(&t.state,t.stack_high-16,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_INVALID);
    t.static_tls_data=BASE+0x24000;assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    assert(read32(t.static_tls_vector)==t.static_tls_data && read32(t.static_tls_data)==0x11223344 && read32(t.static_tls_data+32)==0);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.kernel_handle_capacity=16;
    c.worker_address=BASE+0x40000;c.worker_bytes=0x30000;c.worker_stack_bytes=65536;c.worker_capacity=2;c.worker_host_stack_bytes=1024*1024;c.worker_tls_bytes=argc==3?0:4096;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    struct fnv_native_image foreign=image;foreign.data=arena+8192;
    assert(fnv_windows_bind_imports(process,&foreign,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_FORMAT);
    foreign=image;foreign.base+=65536;
    assert(fnv_windows_bind_imports(process,&foreign,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_FORMAT);
    /* Unsupported callbacks and invalid index must leave IAT and index intact. */
    unsigned char before[8192];memcpy(before,arena,sizeof(before));write32(0x401ba0,0x401c00);
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_UNRESOLVED && !imported);
    write32(0x401ba0,0);assert(!memcmp(before,arena,sizeof(before)));
    write32(0x401b88,0x401bb0);assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_FORMAT);
    write32(0x401b88,0x401ba4);assert(!memcmp(before,arena,sizeof(before)));
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==17 && read32(0x401ba4)==0);
    struct fnv_windows_thread_config t=main_config();assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_INVALID);
    t.static_tls_vector=BASE+0x20000;t.static_tls_vector_bytes=4;t.static_tls_data=BASE+0x21000;t.static_tls_data_bytes=4096;
    struct fnv_windows_thread_config bad=t;bad.static_tls_data++;assert(fnv_windows_thread_attach(process,&bad)==FNV_CPU_INVALID);
    bad=t;bad.static_tls_data=bad.stack_low;assert(fnv_windows_thread_attach(process,&bad)==FNV_CPU_INVALID);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK && read32(t.static_tls_vector)==t.static_tls_data && read32(t.teb_address+0x2c)==t.static_tls_vector);
    if(argc==3){uint32_t args[]={0,0,0x401c00,CTX,0,OUTPUT};struct fnv_cpu_fault fault;
        assert(execute(CREATE,args,&fault)==FNV_CPU_GUEST_FAULT && strstr(fault.text,"TLS budget unavailable"));
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    assert(read32(t.static_tls_data)==0x11223344 && read32(t.static_tls_data+32)==0);
    write32(t.static_tls_data,0x44445555);write32(0x401bb0,0xdeadbeef); /* Snapshot must survive guest template edits. */
    HANDLE ext=CreateThread(NULL,0,external_thread,NULL,0,NULL);assert(ext && WaitForSingleObject(ext,5000)==0);DWORD result;assert(GetExitCodeThread(ext,&result) && !result);CloseHandle(ext);
    for(unsigned trial=0;trial<100;++trial){uint32_t h[2];
        for(unsigned i=0;i<2;++i){context(CTX+i*64,0,0,0);h[i]=spawn(0x401c00,CTX+i*64,4);assert(h[i]);}
        for(unsigned i=0;i<2;++i)assert(call(RESUME,h[i],0,0,0)==1);
        for(unsigned i=0;i<2;++i){assert(call(WAIT,h[i],5000,0,0)==0 && call(CODE,h[i],OUTPUT+4,0,0) && read32(OUTPUT+4)==CTX+i*64);
            assert(read32(CTX+i*64)==0x11223344 && read32(CTX+i*64+4)==0);uint32_t data=read32(CTX+i*64+8);
            assert(data>=c.worker_address && data<c.worker_address+c.worker_bytes && !(data&15));assert(read32(data)==CTX+i*64);assert(call(CLOSE,h[i],0,0,0));}
        assert(read32(CTX+8)!=read32(CTX+64+8) && read32(t.static_tls_data)==0x44445555);settle();
    }
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    puts("{\"status\":\"PASS\",\"static_tls_workers\":200,\"external_attach_checked\":true,\"snapshot_immutable\":true,\"zero_fill_checked\":true,\"index_and_FS_2C_access\":true,\"rejected_bind_preserves_image\":true,\"native_engine_started\":false}");return 0;
}
