#define main unused_file_fixture_main
#include "windows_files_host.c"
#undef main
static uint32_t M(unsigned api,const uint32_t *args){
    const unsigned count[]={2,0,1,7,1};struct fnv_cpu_state s;assert(!fnv_cpu_get_state(&s));
    uint32_t stack=s.fs_base+0xb000;s.gpr[4]=stack;assert(!fnv_cpu_set_state(&s));put32(stack,0x401500);
    for(unsigned i=0;i<count[api];i++)put32(stack+4+i*4,args[i]);
    struct fnv_cpu_fault f;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&f);
    if(r)fprintf(stderr,"Move ABI fault %s\n",f.text);
    assert(!r && !fnv_cpu_get_state(&s) && s.gpr[4]==stack+4+count[api]*4);return s.gpr[0];
}
static void create(const char *p){FILE *f=fopen(p,"wb");assert(f && fwrite("save-test",1,9,f)==9 && !fclose(f));}
int main(int argc,char **argv){
    assert(argc==2);char root[2048];strcpy(root,argv[1]);char *last=strrchr(root,'/'),*back=strrchr(root,'\\');if(back && (!last || back>last))last=back;assert(last);*last=0;strcat(root,"/move-root");
    assert(CreateDirectoryA(root,NULL));char dir[2048];snprintf(dir,sizeof(dir),"%s/UserData",root);assert(CreateDirectoryA(dir,NULL));strcat(dir,"/FNV");assert(CreateDirectoryA(dir,NULL));
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.file_root=root;c.file_handle_capacity=8;
    assert(!fnv_windows_create(&c,&process));struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t bound;assert(!fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&bound) && bound==5);
    struct fnv_windows_thread_config t=config(0);assert(!fnv_windows_thread_attach(process,&t));unsigned checks=0;
    for(unsigned mode=0;mode<6;mode++){
        char src[2048],dst[2048],native_src[2048],native_dst[2048],guest_src[128],guest_dst[128];
        snprintf(src,sizeof(src),"%s/a%u.tmp",dir,mode);snprintf(dst,sizeof(dst),"%s/%sb%u.fos",dir,mode==3?"missing/":"",mode);
        snprintf(native_src,sizeof(native_src),"%s/native%u.tmp",dir,mode);snprintf(native_dst,sizeof(native_dst),"%s/%snative%u.fos",dir,mode==3?"missing/":"",mode);
        snprintf(guest_src,sizeof(guest_src),"C:\\Users\\FNV\\a%u.tmp",mode);snprintf(guest_dst,sizeof(guest_dst),"C:\\Users\\FNV\\%sb%u.fos",mode==3?"missing\\":"",mode);
        if(mode!=2){create(src);create(native_src);}if(mode==1){create(dst);create(native_dst);}
        if(mode==4){strcpy(dst,src);strcpy(native_dst,native_src);strcpy(guest_dst,guest_src);}
        strcpy((char *)arena+NAME_AT-BASE,guest_src);strcpy((char *)arena+NAME_AT+1024-BASE,guest_dst);
        uint32_t error=0x7654;M(2,&error);uint32_t held=0;HANDLE native_held=INVALID_HANDLE_VALUE;
        if(mode==5){uint32_t a[]={NAME_AT,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0};held=M(3,a);assert(held!=UINT32_MAX);native_held=CreateFileA(native_src,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);assert(native_held!=INVALID_HANDLE_VALUE);}
        SetLastError(error);BOOL expected=MoveFileA(native_src,native_dst);DWORD expected_error=GetLastError();uint32_t a[]={NAME_AT,NAME_AT+1024};uint32_t value=M(0,a),actual_error=M(1,NULL);
        if(value!=(uint32_t)expected || actual_error!=expected_error)fprintf(stderr,"mode%u got%u/%u expected%u/%lu\n",mode,value,actual_error,(unsigned)expected,expected_error);
        assert(value==(uint32_t)expected && actual_error==expected_error);
        if(mode==5){assert(M(4,&held) && CloseHandle(native_held));}
        assert((GetFileAttributesA(src)==INVALID_FILE_ATTRIBUTES)==(GetFileAttributesA(native_src)==INVALID_FILE_ATTRIBUTES));
        assert((GetFileAttributesA(dst)==INVALID_FILE_ATTRIBUTES)==(GetFileAttributesA(native_dst)==INVALID_FILE_ATTRIBUTES));checks++;
    }
    assert(!fnv_windows_thread_detach() && !fnv_windows_destroy(process) && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"rename_windows_comparisons\":%u,\"native_engine_started\":false}\n",checks);return 0;
}
