/* Reuse the existing synthetic file ABI callers and fixture helpers. */
#define main fnv_existing_file_test_main
#include "windows_files_host.c"
#undef main
#define SECURITY_AT (BASE+0xB000)

int main(int argc,char **argv){
    assert(argc==2 || argc==3);char root[2048];size_t n=strlen(argv[1]);assert(n<sizeof(root)-80);
    memcpy(root,argv[1],n+1);char *last=strrchr(root,'\\'),*slash=strrchr(root,'/');
    if(slash && (!last || slash>last))last=slash;
    assert(last);*last=0;strcat(root,argc==3?"/security-strict":"/security-root");fixture(root,0);
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;
    c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;
    c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=2;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t imported;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&imported)==FNV_NATIVE_OK && imported==11);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(argc==3){
        uint32_t args[]={path("Data/fixture.bin",0),GENERIC_READ,FILE_SHARE_READ,BASE+ARENA-8,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0},value;
        struct fnv_cpu_fault fault;assert(execute(OPEN_A,args,&value,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK);
        assert(VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;
    }
    char full[2048];assert(strlen(root)+sizeof("/Data/fixture.bin")<=sizeof(full));strcpy(full,root);strcat(full,"/Data/fixture.bin");
    const uint32_t inherit_values[]={0,1,2,0x80000000u};unsigned checks=0;
    for(unsigned i=0;i<2000;++i){
        uint32_t inherit=inherit_values[i%4];put32(SECURITY_AT,12);put32(SECURITY_AT+4,0);put32(SECURITY_AT+8,inherit);
        C(SET_ERROR,0x7654321,0,0,0,0);
        uint32_t h=call(i&1?OPEN_W:OPEN_A,path("Data/fixture.bin",i&1),GENERIC_READ,FILE_SHARE_READ,SECURITY_AT,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
        uint32_t error=C(GET_ERROR,0,0,0,0,0);
        SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,(BOOL)inherit};SetLastError(0x7654321);
        HANDLE native=CreateFileA(full,GENERIC_READ,FILE_SHARE_READ,&sa,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);DWORD native_error=GetLastError();
        if(h==UINT32_MAX || native==INVALID_HANDLE_VALUE || error!=native_error)
            fprintf(stderr,"security case=%u inherit=%08x guest=%08x error=%u native=%p error=%lu\n",i,inherit,h,error,native,native_error);
        assert(h!=UINT32_MAX && native!=INVALID_HANDLE_VALUE && error==native_error);
        DWORD flags;assert(GetHandleInformation(native,&flags) && !!(flags&HANDLE_FLAG_INHERIT)==!!inherit);
        DWORD count=0;unsigned char bytes[64];assert(ReadFile(native,bytes,sizeof(bytes),&count,NULL) && count==sizeof(bytes));
        assert(C(READ,h,BUFFER,sizeof(bytes),OUTPUT,0) && read32(OUTPUT)==sizeof(bytes) && !memcmp(bytes,arena+BUFFER-BASE,sizeof(bytes)));
        assert(C(CLOSE,h,0,0,0,0) && CloseHandle(native));checks++;
    }
    /* This bounded bridge still rejects custom ACLs and malformed layouts. */
    put32(SECURITY_AT,11);
    assert(call(OPEN_A,path("Data/fixture.bin",0),GENERIC_READ,1,SECURITY_AT,3,0x80,0)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==50);
    put32(SECURITY_AT,12);put32(SECURITY_AT+4,SECURITY_AT+16);
    assert(call(OPEN_A,path("Data/fixture.bin",0),GENERIC_READ,1,SECURITY_AT,3,0x80,0)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==50);
    put32(SECURITY_AT+4,0);put32(SECURITY_AT+8,1);
    assert(call(OPEN_A,path("../escape",0),GENERIC_READ,1,SECURITY_AT,3,0x80,0)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==5);
    assert(call(OPEN_A,path("Data/forbidden.bin",0),GENERIC_WRITE,1,SECURITY_AT,CREATE_ALWAYS,0x80,0)==UINT32_MAX && C(GET_ERROR,0,0,0,0,0)==50);
    struct fnv_windows_file_stats stats;assert(fnv_windows_file_stats(process,&stats)==FNV_CPU_OK && stats.opens==checks && !stats.live_handles && !stats.read_errors);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"security_attribute_opens\":%u,\"inheritance_bool_values\":4,\"unsupported_cases\":4,\"native_engine_started\":false}\n",checks);return 0;
}
