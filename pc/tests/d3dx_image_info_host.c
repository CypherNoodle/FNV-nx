#define main fnv_existing_files_main
#include "windows_files_host.c"
#undef main
#include "windows_d3dx.h"
#include "dds_image_info.h"
static uint32_t I(unsigned api,uint32_t a,uint32_t b,uint32_t c,int negative){
    const unsigned args[]={3,0,1};struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);
    s.gpr[4]=s.fs_base+0xb000;uint32_t stack=s.gpr[4];assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    put32(stack,0x401500);put32(stack+4,a);put32(stack+8,b);put32(stack+12,c);
    struct fnv_cpu_fault f;enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*64,&f);
    if(negative){assert(r==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED);return 0;}
    if(r!=FNV_CPU_OK)fprintf(stderr,"image metadata: %s\n",f.text);
    assert(r==FNV_CPU_OK && fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==stack+4+args[api]*4);return s.gpr[0];
}
static unsigned char dds[262144];
static void header(unsigned width,unsigned height,unsigned levels,unsigned format,unsigned shape){
    memset(dds,0,sizeof(dds));const uint32_t common[][2]={{0,0x20534444},{4,124},{8,0x21007},{76,32},{108,0x1000}};
    for(unsigned i=0;i<5;++i)fnv_dds_put32(dds+common[i][0],common[i][1]);
    fnv_dds_put32(dds+12,height);fnv_dds_put32(dds+16,width);fnv_dds_put32(dds+28,levels);
    if(format<5){fnv_dds_put32(dds+80,4);fnv_dds_put32(dds+84,0x31545844+format*0x1000000);}
    else {const uint32_t pixel[][6]={
        {0x41,32,0xff0000,0xff00,0xff,0xff000000},{0x40,32,0xff0000,0xff00,0xff,0},
        {0x40,16,0xf800,0x7e0,0x1f,0},{2,8,0,0,0,0xff},{0x20000,8,0xff,0,0,0},
        {0x41,32,0xff,0xff00,0xff0000,0xff000000},{0x40,32,0xff,0xff00,0xff0000,0},
        {0x40,16,0x7c00,0x3e0,0x1f,0},{0x41,16,0x7c00,0x3e0,0x1f,0x8000},
        {0x41,16,0xf00,0xf0,0xf,0xf000},{0x20001,16,0xff,0,0,0xff00}};
        assert(format<16);const uint32_t *v=pixel[format-5];fnv_dds_put32(dds+80,v[0]);
        for(unsigned i=1;i<6;++i)fnv_dds_put32(dds+84+4*i,v[i]);}
    if(shape==1)fnv_dds_put32(dds+112,0xfe00);
    if(shape==2){fnv_dds_put32(dds+8,0x821007);fnv_dds_put32(dds+24,4);fnv_dds_put32(dds+112,0x200000);}
}
int main(int argc,char **argv){
    assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
    size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_d3dx_exports(&n);assert(n==3);
    struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,exports,n,&bound)==FNV_NATIVE_OK && bound==3);
    struct fnv_windows_thread_config t=config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    header(16,16,1,0,0);memcpy(arena+BUFFER-BASE,dds,sizeof(dds));
    if(argc==3){if(!strcmp(argv[2],"--strict-input"))I(0,BASE+ARENA-64,128,OUTPUT,1);
        else if(!strcmp(argv[2],"--strict-output"))I(0,BUFFER,sizeof(dds),BASE+ARENA-16,1);
        else {put32(BUFFER+84,0x30315844);I(0,BUFFER,sizeof(dds),OUTPUT,1);}
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));puts("strict PASS");return 0;}
    HMODULE dll=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(dll);
    typedef HRESULT (WINAPI *info_fn)(const void *,UINT,void *);FARPROC proc=GetProcAddress(dll,"D3DXGetImageInfoFromFileInMemory");info_fn info;memcpy(&info,&proc,sizeof(info));assert(info);
    unsigned checks=0;
    for(unsigned ncase=0;ncase<4800;++ncase){unsigned format=ncase%16,shape=ncase/16%3,width=1+(ncase*17)%64,height=shape==1?width:1+(ncase*23)%64;
        header(width,height,ncase%9,format,shape);unsigned size=sizeof(dds);
        switch(ncase%13){case 0:size=0;break;case 1:size=1;break;case 2:size=127;break;case 3:size=128;break;
            case 4:fnv_dds_put32(dds+76,0);break;case 5:fnv_dds_put32(dds,0);break;case 6:fnv_dds_put32(dds+4,0);break;
            case 7:size=129+(ncase*127)%16000;break;default:break;}
        for(unsigned mode=0;mode<3;++mode){uint32_t src=BUFFER+1,dst=mode==1?src+17:OUTPUT+1;
            memset(arena+OUTPUT-BASE,0xa5,40);memcpy(arena+src-BASE,dds,sizeof(dds));
            unsigned char expected[28];memcpy(expected,arena+dst-BASE,28);SetLastError(0x8765);
            uint32_t wanted=(uint32_t)info(dds,size,mode==2?NULL:expected);DWORD error=GetLastError();
            I(2,0x8765,0,0,0);unsigned char before=arena[dst-BASE-1],after=arena[dst-BASE+28];
            uint32_t got=I(0,src,size,mode==2?0:dst,0);
            if(got!=wanted || (mode!=2 && memcmp(arena+dst-BASE,expected,28)))fprintf(stderr,"case %u fmt %u shape %u size %u got %08x wanted %08x\n",ncase,format,shape,size,got,wanted);
            assert(got==wanted && I(1,0,0,0,0)==error);if(mode!=2)assert(!memcmp(arena+dst-BASE,expected,28));
            assert(arena[dst-BASE-1]==before && arena[dst-BASE+28]==after);checks++;
        }
    }
    assert(I(0,0,100,OUTPUT,0)==0x8876086c);
    FreeLibrary(dll);assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"native_dds_metadata_checks\":%u,\"engine_started\":false}\n",checks);return 0;
}
