#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
#include "d3d9_com.h"
#include "guest_memory.h"
#include "memory_horizon.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateVertexShader)==91*sizeof(void*),"D3D9 vtable");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetVertexShaderConstantF)==94*sizeof(void*),"D3D9 vtable");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetPixelShaderConstantF)==109*sizeof(void*),"D3D9 vtable");
_Static_assert(offsetof(IDirect3DVertexShader9Vtbl,GetFunction)==4*sizeof(void*),"Shader vtable");
#define BASE 0x400000u
#define ARENA (32u<<20)
#define POOL 0x1000000u
#define GUEST_OUT (BASE+0x9000)
#define DATA (BASE+0xA000)
enum { QUERY,ADDREF,RELEASE,GET_DEVICE,GET_FUNCTION,CREATE_VS,CREATE_PS,VS_SF,VS_GF,VS_SI,VS_GI,VS_SB,VS_GB,PS_SF,PS_GF,PS_SI,PS_GI,PS_SB,PS_GB,UNSUPPORTED };
static unsigned char *arena;
static struct fnv_cpu_runtime *cpu;
static struct fnv_d3d9_com *com;
static struct fnv_guest_memory *vm;
static IDirect3DDevice9 *device;
static IDirect3D9 *api;
static struct fnv_d3d9_shader_code *catalog;
static unsigned catalog_count,created,deleted,shader_calls,constant_calls,failed_create;
static uint32_t device_at,code_at,dummy_token;
static uintptr_t last_shader;
static unsigned last_stage;
static unsigned reuse_handle;
static _Thread_local uint32_t stack_offset=4096;
static unsigned char *ptr(uint32_t at){assert(at>=BASE && (uint64_t)at<BASE+ARENA);return arena+(at-BASE);}
static void put(uint32_t at,uint32_t v){unsigned char *p=ptr(at);for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t get(uint32_t at){unsigned char *p=ptr(at);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t read_file32(FILE *f){uint32_t v;assert(fread(&v,1,4,f)==4);return v;}
static uint32_t set_permission(void *ctx,void *p,uint32_t n,uint32_t permission){(void)ctx;DWORD old;return VirtualProtect(p,n,permission==3?PAGE_READWRITE:permission==1?PAGE_READONLY:PAGE_NOACCESS,&old)?0:GetLastError();}
static struct fnv_vm_permissions permissions={NULL,set_permission};
static uint32_t backend_create(void *ctx,enum fnv_d3d9_stage stage,const void *bytes,uint32_t size,uintptr_t *out) {
    (void)ctx;(void)size;if(failed_create)return FNV_D3D9_INVALIDCALL;
    if(reuse_handle){assert(last_shader);IUnknown_AddRef((IUnknown*)last_shader);*out=last_shader;created++;return 0;}
    HRESULT h;void *s=NULL;
    if(stage==FNV_D3D9_VERTEX)h=IDirect3DDevice9_CreateVertexShader(device,bytes,(IDirect3DVertexShader9**)&s);
    else h=IDirect3DDevice9_CreatePixelShader(device,bytes,(IDirect3DPixelShader9**)&s);
    if(SUCCEEDED(h)){*out=(uintptr_t)s;last_shader=(uintptr_t)s;last_stage=stage;created++;}return (uint32_t)h;
}
static void backend_addref(void *ctx,uintptr_t shader){(void)ctx;IUnknown_AddRef((IUnknown*)shader);}
static void backend_release(void *ctx,uintptr_t shader){(void)ctx;if(!IUnknown_Release((IUnknown*)shader))deleted++;}
static void device_addref(void *ctx){(void)ctx;IDirect3DDevice9_AddRef(device);}
static void device_release(void *ctx){(void)ctx;IDirect3DDevice9_Release(device);}
static void dummy(void){fnv_cpu_return_u32(123,0);}
static void registry_checks(void) {
    fnv_cpu_function good=dummy;uint32_t old=0;
    assert(fnv_cpu_register_native(cpu,&good,1,&old)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,old,1)==FNV_CPU_OK);
    assert(fnv_cpu_register_native(cpu,&good,1,&dummy_token)==FNV_CPU_OK && dummy_token==old+4);
    fnv_cpu_function bad[2]={dummy,NULL};uint32_t unchanged[2]={111,222};
    assert(fnv_cpu_register_native(cpu,bad,2,unchanged)==FNV_CPU_INVALID && unchanged[0]==111 && unchanged[1]==222);
    fnv_cpu_function many[FNV_CPU_MAX_NATIVE_METHODS];uint32_t tokens[FNV_CPU_MAX_NATIVE_METHODS];
    for(unsigned i=0;i<FNV_CPU_MAX_NATIVE_METHODS;i++){many[i]=dummy;tokens[i]=77;}
    assert(fnv_cpu_register_native(cpu,many,FNV_CPU_MAX_NATIVE_METHODS,tokens)==FNV_CPU_NO_MEMORY);
    for(unsigned i=0;i<FNV_CPU_MAX_NATIVE_METHODS;i++)assert(tokens[i]==77);
    assert(fnv_cpu_unregister_native(cpu,old,1)==FNV_CPU_INVALID);
    assert(fnv_cpu_destroy(cpu)==FNV_CPU_BUSY);
}
static enum fnv_cpu_result invoke(unsigned method,unsigned argc,const uint32_t *args,uint32_t *result,struct fnv_cpu_fault *fault) {
    struct fnv_cpu_state s,after;fnv_cpu_state_init(&s,BASE+ARENA-stack_offset,0);
    s.gpr[1]=0xDEADBEEF;s.gpr[3]=1234;s.gpr[6]=5678;s.gpr[7]=91011;
    put(s.gpr[4],BASE+0x1000);for(unsigned i=0;i<argc;i++)put(s.gpr[4]+4+i*4,args[i]);
    assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
    enum fnv_cpu_result r=fnv_cpu_execute(0x401500+method*64,fault);
    if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&after)==FNV_CPU_OK);
        assert(after.gpr[4]==s.gpr[4]+4+argc*4 && after.gpr[3]==1234 && after.gpr[6]==5678 && after.gpr[7]==91011);if(result)*result=after.gpr[0];}
    return r;
}
static uint32_t call(unsigned method,unsigned argc,const uint32_t *args){uint32_t v;assert(invoke(method,argc,args,&v,NULL)==FNV_CPU_OK);return v;}
static uint32_t release(uint32_t at){return call(RELEASE,1,&at);}
static uint32_t make_shader(unsigned index) {
    const struct fnv_d3d9_shader_code *s=catalog+index;assert(s->size<=1048576);memcpy(ptr(code_at),s->bytes,s->size);put(GUEST_OUT,0xCCCCCCCC);
    unsigned stage=((const uint32_t*)s->bytes)[0]>>16==0xFFFF;uint32_t args[]={device_at,code_at,GUEST_OUT};
    assert(!call(stage?CREATE_PS:CREATE_VS,3,args));uint32_t shader=get(GUEST_OUT);assert(shader && shader!=device_at && shader<FNV_CPU_NATIVE_METHOD_BASE);
    return shader;
}
static int upload(void *ctx,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const uint32_t *data) {
    (void)ctx;HRESULT h;
    if(!s){if(!k)h=IDirect3DDevice9_SetVertexShaderConstantF(device,first,(const float*)data,count);
        else if(k==1)h=IDirect3DDevice9_SetVertexShaderConstantI(device,first,(const int*)data,count);
        else h=IDirect3DDevice9_SetVertexShaderConstantB(device,first,(const BOOL*)data,count);}
    else {if(!k)h=IDirect3DDevice9_SetPixelShaderConstantF(device,first,(const float*)data,count);
        else if(k==1)h=IDirect3DDevice9_SetPixelShaderConstantI(device,first,(const int*)data,count);
        else h=IDirect3DDevice9_SetPixelShaderConstantB(device,first,(const BOOL*)data,count);}
    return SUCCEEDED(h);
}
static void constants(void) {
    for(unsigned s=0;s<2;s++)for(unsigned k=0;k<3;k++)for(unsigned n=0;n<40;n++) {
        unsigned first=n%8,count=1+n%8,width=k==2?1:4;uint32_t bits[32],native[32];
        for(unsigned i=0;i<count*width;i++)bits[i]=(n*2654435761u)^i^(s<<20)^(k<<24);
        memcpy(ptr(DATA),bits,count*width*4);uint32_t args[]={device_at,first,DATA,count};unsigned setter=(s?PS_SF:VS_SF)+k*2;
        assert(!call(setter,4,args));memset(ptr(DATA),0,count*width*4);assert(!call(setter+1,4,args));assert(!memcmp(ptr(DATA),bits,count*width*4));
        assert(!fnv_d3d9_com_flush(com,upload,NULL));HRESULT h;
        if(!s){if(!k)h=IDirect3DDevice9_GetVertexShaderConstantF(device,first,(float*)native,count);
            else if(k==1)h=IDirect3DDevice9_GetVertexShaderConstantI(device,first,(int*)native,count);
            else h=IDirect3DDevice9_GetVertexShaderConstantB(device,first,(BOOL*)native,count);}
        else {if(!k)h=IDirect3DDevice9_GetPixelShaderConstantF(device,first,(float*)native,count);
            else if(k==1)h=IDirect3DDevice9_GetPixelShaderConstantI(device,first,(int*)native,count);
            else h=IDirect3DDevice9_GetPixelShaderConstantB(device,first,(BOOL*)native,count);}
        assert(SUCCEEDED(h) && !memcmp(native,bits,count*width*4));constant_calls++;
    }
    uint32_t bad[]={device_at,255,DATA,2};assert(call(VS_SF,4,bad)==FNV_D3D9_INVALIDCALL);
    uint32_t empty[]={device_at,256,0,0};assert(!call(VS_SF,4,empty));
    uint32_t bad_out[]={device_at,0,POOL+7u*1024*1024,1};assert(call(VS_GF,4,bad_out)==FNV_D3D9_INVALIDCALL);
}
static HANDLE start_gate;
static DWORD WINAPI worker(void *opaque) {
    unsigned stage=(unsigned)(uintptr_t)opaque;stack_offset=8192+stage*4096;
    struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-stack_offset,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);
    assert(WaitForSingleObject(start_gate,INFINITE)==WAIT_OBJECT_0);
    uint32_t data=DATA+0x10000+stage*4096;
    for(unsigned n=0;n<200;n++) {
        uint32_t bits[4]={stage,n,~n,0x80000000};memcpy(ptr(data),bits,16);
        uint32_t args[]={device_at,4,data,1};unsigned setter=stage?PS_SF:VS_SF;
        assert(!call(setter,4,args));memset(ptr(data),0,16);assert(!call(setter+1,4,args) && !memcmp(ptr(data),bits,16));
        assert(call(ADDREF,1,&device_at)>=2);assert(release(device_at)>=1);
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK);return 0;
}
static void concurrent(void) {
    start_gate=CreateEventA(NULL,TRUE,FALSE,NULL);assert(start_gate);
    HANDLE threads[2]={CreateThread(NULL,0,worker,NULL,0,NULL),CreateThread(NULL,0,worker,(void*)(uintptr_t)1,0,NULL)};
    assert(threads[0] && threads[1] && SetEvent(start_gate));assert(WaitForMultipleObjects(2,threads,TRUE,INFINITE)==WAIT_OBJECT_0);
    for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code);CloseHandle(threads[i]);}CloseHandle(start_gate);
}
static void shader_roundtrips(void) {
    for(unsigned i=0;i<catalog_count;i++) {
        uint32_t shader=make_shader(i);const struct fnv_d3d9_shader_code *s=catalog+i;
        uint32_t args[]={shader,0,GUEST_OUT+4};put(GUEST_OUT+4,0);assert(!call(GET_FUNCTION,3,args) && get(GUEST_OUT+4)==s->size);
        memset(ptr(code_at),0xA5,s->size);args[1]=code_at;assert(!call(GET_FUNCTION,3,args) && !memcmp(ptr(code_at),s->bytes,s->size));
        args[1]=DATA;put(GUEST_OUT+4,1);put(DATA,0xCCCCCCCC);
        UINT native_size=1;unsigned char *native_copy=malloc(s->size);assert(native_copy);memset(native_copy,0xCC,s->size);
        HRESULT h=last_stage?
            IDirect3DPixelShader9_GetFunction((IDirect3DPixelShader9*)last_shader,native_copy,&native_size):
            IDirect3DVertexShader9_GetFunction((IDirect3DVertexShader9*)last_shader,native_copy,&native_size);
        uint32_t hr=call(GET_FUNCTION,3,args);
        if(hr!=(uint32_t)h || get(GUEST_OUT+4)!=native_size){fprintf(stderr,"short buffer own=%08x/%u native=%08lx/%u\n",hr,get(GUEST_OUT+4),(unsigned long)h,native_size);abort();}
        assert(!memcmp(ptr(DATA),native_copy,s->size));free(native_copy);
        if(i<8) {
            const GUID *iid=last_stage?&IID_IDirect3DPixelShader9:&IID_IDirect3DVertexShader9;
            memcpy(ptr(DATA),iid,16);uint32_t qi[]={shader,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==shader);assert(release(shader)==1);
            memset(ptr(DATA),0xA7,16);assert(call(QUERY,3,qi)==0x80004002 && !get(GUEST_OUT));
            uint32_t gd[]={shader,GUEST_OUT};assert(!call(GET_DEVICE,2,gd) && get(GUEST_OUT)==device_at);release(device_at);
            assert(call(ADDREF,1,&shader)==2 && release(shader)==1);
        }
        assert(!release(shader));shader_calls++;
    }
}
int main(int argc,char **argv) {
    assert(argc==4);FILE *f=fopen(argv[2],"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9SHD1",8));
    catalog_count=read_file32(f);assert(catalog_count && catalog_count<=8192);catalog=calloc(catalog_count,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<catalog_count;i++){uint32_t stage=read_file32(f),size=read_file32(f);assert(stage<2 && size>=8 && size<=1048576);void *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);catalog[i]=(struct fnv_d3d9_shader_code){bytes,size};}
    assert(fgetc(f)==EOF);fclose(f);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config config={0};config.memory=arena;config.guest_base=BASE;config.memory_bytes=ARENA;config.code_low=BASE+0x1000;config.code_high=BASE+0x2000;
    assert(fnv_cpu_create(&config,&cpu)==FNV_CPU_OK);registry_checks();
    struct fnv_vm_config mc={ptr(POOL),POOL,8u<<20,8u<<20,&permissions,fnv_vm_eager_apply};assert(!fnv_vm_create(&mc,&vm).error);
    uint32_t objects=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(objects);
    code_at=fnv_vm_allocate(vm,0,1048576,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(code_at);
    HWND window=CreateWindowExA(0,"STATIC","FNV COM oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
    api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=objects,.objects_bytes=65536,.resource_capacity=4096,.vertex_floats=256,.pixel_floats=224,
        .catalog=catalog,.catalog_count=catalog_count,.backend={.context=NULL,.create=backend_create,.addref=backend_addref,.release=backend_release,
        .device_addref=device_addref,.device_release=device_release},.memory=vm};
    if(!strcmp(argv[3],"capacity"))cc.resource_capacity=1;
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&initial)==FNV_CPU_OK);
    fnv_cpu_function good=dummy;uint32_t untouched=123;assert(fnv_cpu_register_native(cpu,&good,1,&untouched)==FNV_CPU_BUSY && untouched==123);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_BUSY && fnv_d3d9_com_destroy(com)==FNV_CPU_BUSY);
    struct fnv_cpu_fault fault;unsigned faulted=0;
    if(!strcmp(argv[3],"normal")){concurrent();constants();shader_roundtrips();
        memcpy(ptr(DATA),&IID_IDirect3DDevice9,16);uint32_t qi[]={device_at,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==device_at);assert(release(device_at)==1);
        memset(ptr(DATA),0,16);ptr(DATA)[8]=0xC0;ptr(DATA)[15]=0x46;assert(!call(QUERY,3,qi) && get(GUEST_OUT)==device_at);assert(release(device_at)==1);
        failed_create=1;memcpy(ptr(code_at),catalog[0].bytes,catalog[0].size);uint32_t args[]={device_at,code_at,GUEST_OUT};put(GUEST_OUT,123);
        unsigned stage=((const uint32_t*)catalog[0].bytes)[0]>>16==0xFFFF;
        assert(call(stage?CREATE_PS:CREATE_VS,3,args)==FNV_D3D9_INVALIDCALL && !get(GUEST_OUT));failed_create=0;}
    else if(!strcmp(argv[3],"parent")){uint32_t shader=make_shader(0);assert(release(device_at)==1);uint32_t args[]={shader,GUEST_OUT};
        assert(!call(GET_DEVICE,2,args) && get(GUEST_OUT)==device_at);assert(release(device_at)==1);assert(!release(shader));}
    else if(!strcmp(argv[3],"capacity")){uint32_t shader=make_shader(0);assert(!release(shader));uint32_t recycled=make_shader(0);assert(shader==recycled);
        assert(catalog_count>1);memcpy(ptr(code_at),catalog[1].bytes,catalog[1].size);
        unsigned stage=((const uint32_t*)catalog[1].bytes)[0]>>16==0xFFFF;uint32_t args[]={device_at,code_at,GUEST_OUT};put(GUEST_OUT,123);
        assert(call(stage?CREATE_PS:CREATE_VS,3,args)==FNV_D3D9_OUTOFMEMORY && !get(GUEST_OUT));assert(!release(recycled) && created==deleted);}
    else if(!strcmp(argv[3],"alias")){uint32_t shader=make_shader(0);reuse_handle=1;assert(make_shader(0)==shader);reuse_handle=0;assert(release(shader)==1 && !release(shader));}
    else if(!strcmp(argv[3],"unknown")){memset(ptr(code_at),0,4096);uint32_t args[]={device_at,code_at,GUEST_OUT};assert(invoke(CREATE_VS,3,args,NULL,&fault)==FNV_CPU_GUEST_FAULT);faulted=1;}
    else if(!strcmp(argv[3],"unsupported")){uint32_t args[]={device_at,0};assert(invoke(UNSUPPORTED,2,args,NULL,&fault)==FNV_CPU_GUEST_FAULT);faulted=1;}
    else if(!strcmp(argv[3],"stale")){uint32_t shader=make_shader(0);assert(!release(shader));assert(invoke(ADDREF,1,&shader,NULL,&fault)==FNV_CPU_GUEST_FAULT);faulted=1;}
    else {assert(!strcmp(argv[3],"unregistered"));assert(fnv_cpu_execute(FNV_CPU_NATIVE_METHOD_BASE,&fault)==FNV_CPU_GUEST_FAULT);faulted=1;}
    if(faulted){assert(fault.kind==(!strcmp(argv[3],"unregistered")?FNV_CPU_UNKNOWN_TARGET:FNV_CPU_UNIMPLEMENTED));assert(fnv_cpu_execute(0x401500,NULL)==FNV_CPU_FAULTED);}
    assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_cpu_destroy(cpu)==FNV_CPU_BUSY);assert(fnv_d3d9_com_destroy(com)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(!fnv_vm_destroy(vm).error);assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    for(unsigned i=0;i<catalog_count;i++)free((void*)catalog[i].bytes);free(catalog);
    printf("{\"status\":\"PASS\",\"shader_roundtrips\":%u,\"constant_roundtrips\":%u,\"backend_creates\":%u,\"backend_deleted\":%u,\"prepared_methods\":47,\"concurrent_constant_roundtrips\":%u}\n",shader_calls,constant_calls,created,deleted,!strcmp(argv[3],"normal")?400:0);return 0;
}
