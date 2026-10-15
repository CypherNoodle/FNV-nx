/* Reuse the verified CPU/COM fixture and lifecycle fault cases. */
#define main baseline_main
#include "d3d9_com_host.c"
#undef main
enum { SET_VS=19,GET_VS=20,SET_PS=21,GET_PS=22,CREATE_VB=23,CREATE_IB=24,LOCK_BUFFER=25,UNLOCK_BUFFER=26,
    GET_DESC=27,GET_TYPE=28,SET_STREAM=29,GET_STREAM=30,SET_INDICES=31,GET_INDICES=32,SET_FREQUENCY=33,GET_FREQUENCY=34,DRAW_PENDING=35,CREATE_DECL=36,SET_DECL=37,GET_DECL=38,GET_ELEMENTS=39 };
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateVertexBuffer)==26*sizeof(void*),"VB slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetStreamSource)==100*sizeof(void*),"stream slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetIndices)==104*sizeof(void*),"index slot");
_Static_assert(offsetof(IDirect3DVertexBuffer9Vtbl,Lock)==11*sizeof(void*),"lock slot");
_Static_assert(sizeof(D3DVERTEXBUFFER_DESC)==24 && sizeof(D3DINDEXBUFFER_DESC)==20,"guest descriptor sizes");
static unsigned buffer_creates,updates,binds,fail_buffer,fail_update,fail_bind;
static uintptr_t last_buffer,last_declaration;
static unsigned declaration_creates,declaration_roundtrips;
static uint32_t shadow_base;
static const uint32_t shadow_bytes=4u<<20;
static uint32_t create_native_declaration(void *ctx,const void *elements,uint32_t count,uintptr_t *out) {
    (void)ctx;assert(count && count<=65);if(fail_buffer)return 0x80004005;
    IDirect3DVertexDeclaration9 *decl=NULL;
    HRESULT h=IDirect3DDevice9_CreateVertexDeclaration(device,elements,&decl);
    if(SUCCEEDED(h)){declaration_creates++;last_declaration=*out=(uintptr_t)decl;}return (uint32_t)h;
}
static uint32_t set_declaration(void *ctx,uintptr_t decl) {
    (void)ctx;if(fail_bind)return 0x80004005;
    return (uint32_t)IDirect3DDevice9_SetVertexDeclaration(device,(IDirect3DVertexDeclaration9*)decl);
}
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateVertexDeclaration)==86*sizeof(void*),"declaration slot");
_Static_assert(sizeof(D3DVERTEXELEMENT9)==8 && MAXD3DDECLLENGTH==64,"packed declaration ABI");
static uint32_t set_shader(void *ctx,enum fnv_d3d9_stage stage,uintptr_t handle) {
    (void)ctx;if(fail_bind)return 0x80004005;binds++;
    return stage?IDirect3DDevice9_SetPixelShader(device,(IDirect3DPixelShader9*)handle):IDirect3DDevice9_SetVertexShader(device,(IDirect3DVertexShader9*)handle);
}
static uint32_t create_native_buffer(void *ctx,enum fnv_d3d9_resource_kind kind,uint32_t bytes,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out) {
    (void)ctx;if(fail_buffer)return 0x80004005;void *buffer=NULL;HRESULT h;
    if(kind==FNV_D3D9_VB)h=IDirect3DDevice9_CreateVertexBuffer(device,bytes,usage,format,pool,(IDirect3DVertexBuffer9**)&buffer,NULL);
    else h=IDirect3DDevice9_CreateIndexBuffer(device,bytes,usage,format,pool,(IDirect3DIndexBuffer9**)&buffer,NULL);
    if(SUCCEEDED(h)){buffer_creates++;*out=(uintptr_t)buffer;last_buffer=(uintptr_t)buffer;}return (uint32_t)h;
}
static uint32_t update_native_buffer(void *ctx,uintptr_t handle,uint32_t offset,uint32_t bytes,uint32_t flags,const void *data) {
    (void)ctx;if(fail_update)return 0x80004005;void *mapped=NULL;HRESULT h;D3DRESOURCETYPE type=IDirect3DResource9_GetType((IDirect3DResource9*)handle);
    if(type==D3DRTYPE_VERTEXBUFFER)h=IDirect3DVertexBuffer9_Lock((IDirect3DVertexBuffer9*)handle,offset,bytes,&mapped,flags);
    else h=IDirect3DIndexBuffer9_Lock((IDirect3DIndexBuffer9*)handle,offset,bytes,&mapped,flags);
    if(FAILED(h))return (uint32_t)h;memcpy(mapped,data,bytes);
    h=type==D3DRTYPE_VERTEXBUFFER?IDirect3DVertexBuffer9_Unlock((IDirect3DVertexBuffer9*)handle):IDirect3DIndexBuffer9_Unlock((IDirect3DIndexBuffer9*)handle);
    if(SUCCEEDED(h))updates++;return (uint32_t)h;
}
static uint32_t set_stream(void *ctx,uint32_t stream,uintptr_t buffer,uint32_t offset,uint32_t stride) {
    (void)ctx;if(fail_bind)return 0x80004005;binds++;
    return (uint32_t)IDirect3DDevice9_SetStreamSource(device,stream,(IDirect3DVertexBuffer9*)buffer,offset,stride);
}
static uint32_t set_indices(void *ctx,uintptr_t buffer){(void)ctx;if(fail_bind)return 0x80004005;binds++;return (uint32_t)IDirect3DDevice9_SetIndices(device,(IDirect3DIndexBuffer9*)buffer);}
static uint32_t set_frequency(void *ctx,uint32_t stream,uint32_t setting){(void)ctx;return (uint32_t)IDirect3DDevice9_SetStreamSourceFreq(device,stream,setting);}
static uint32_t buffer(unsigned index_buffer,uint32_t bytes,uint32_t usage,uint32_t pool) {
    uint32_t args[]={device_at,bytes,usage,index_buffer?D3DFMT_INDEX16:0,pool,GUEST_OUT,0};
    assert(!call(index_buffer?CREATE_IB:CREATE_VB,7,args));return get(GUEST_OUT);
}
static uint32_t lock_buffer(uint32_t resource,uint32_t offset,uint32_t bytes,uint32_t flags) {
    uint32_t args[]={resource,offset,bytes,GUEST_OUT,flags};put(GUEST_OUT,0);
    assert(!call(LOCK_BUFFER,5,args));uint32_t at=get(GUEST_OUT);
    assert(at>=shadow_base && (uint64_t)at+bytes<=(uint64_t)shadow_base+shadow_bytes);return at;
}
static void unlock_buffer(uint32_t resource){assert(!call(UNLOCK_BUFFER,1,&resource));}
static void check_bytes(uintptr_t handle,const void *expected,uint32_t size) {
    void *data=NULL;D3DRESOURCETYPE type=IDirect3DResource9_GetType((IDirect3DResource9*)handle);
    HRESULT h=type==D3DRTYPE_VERTEXBUFFER?IDirect3DVertexBuffer9_Lock((IDirect3DVertexBuffer9*)handle,0,size,&data,D3DLOCK_READONLY):
        IDirect3DIndexBuffer9_Lock((IDirect3DIndexBuffer9*)handle,0,size,&data,D3DLOCK_READONLY);
    assert(SUCCEEDED(h) && !memcmp(data,expected,size));
    if(type==D3DRTYPE_VERTEXBUFFER)assert(SUCCEEDED(IDirect3DVertexBuffer9_Unlock((IDirect3DVertexBuffer9*)handle)));
    else assert(SUCCEEDED(IDirect3DIndexBuffer9_Unlock((IDirect3DIndexBuffer9*)handle)));
}
static void descriptions(uint32_t resource,uintptr_t native,unsigned index_buffer) {
    uint32_t args[]={resource,DATA};assert(!call(GET_DESC,2,args));assert(call(GET_TYPE,1,&resource)==(index_buffer?7:6));
    if(index_buffer){D3DINDEXBUFFER_DESC d;assert(SUCCEEDED(IDirect3DIndexBuffer9_GetDesc((IDirect3DIndexBuffer9*)native,&d)));assert(!memcmp(&d,ptr(DATA),20));}
    else {D3DVERTEXBUFFER_DESC d;assert(SUCCEEDED(IDirect3DVertexBuffer9_GetDesc((IDirect3DVertexBuffer9*)native,&d)));assert(!memcmp(&d,ptr(DATA),24));}
    memcpy(ptr(DATA),index_buffer?&IID_IDirect3DIndexBuffer9:&IID_IDirect3DVertexBuffer9,16);
    uint32_t qi[]={resource,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==resource);assert(release(resource)==1);
    memcpy(ptr(DATA),&IID_IDirect3DResource9,16);assert(!call(QUERY,3,qi) && get(GUEST_OUT)==resource);assert(release(resource)==1);
}
static void shader_bindings(void) {
    for(unsigned stage=0;stage<2;stage++) {
        unsigned index=0;while(index<catalog_count && ((((const uint32_t*)catalog[index].bytes)[0]>>16==0xFFFF)!=stage))index++;assert(index<catalog_count);
        uint32_t shader=make_shader(index);uintptr_t native=last_shader;unsigned setter=stage?SET_PS:SET_VS,getter=stage?GET_PS:GET_VS;
        uint32_t args[]={device_at,shader};assert(!call(setter,2,args));
        void *actual=NULL;HRESULT h=stage?IDirect3DDevice9_GetPixelShader(device,(IDirect3DPixelShader9**)&actual):IDirect3DDevice9_GetVertexShader(device,(IDirect3DVertexShader9**)&actual);
        assert(SUCCEEDED(h) && (uintptr_t)actual==native);IUnknown_Release((IUnknown*)actual);
        assert(!release(shader));args[1]=GUEST_OUT;assert(!call(getter,2,args) && get(GUEST_OUT)==shader);
        uint32_t gd[]={shader,GUEST_OUT};assert(!call(GET_DEVICE,2,gd) && get(GUEST_OUT)==device_at);release(device_at);assert(!release(shader));
        fail_bind=1;args[1]=0;assert(call(setter,2,args)==0x80004005);fail_bind=0;
        args[1]=GUEST_OUT;assert(!call(getter,2,args) && get(GUEST_OUT)==shader);assert(!release(shader));
        args[1]=0;assert(!call(setter,2,args));args[1]=GUEST_OUT;put(GUEST_OUT,123);assert(!call(getter,2,args) && !get(GUEST_OUT));
    }
}
static void buffer_bindings(uint32_t vb,uint32_t ib,uintptr_t native_vb,uintptr_t native_ib) {
    uint32_t stream[]={device_at,0,vb,0,20},indices[]={device_at,ib};assert(!call(SET_STREAM,5,stream) && !call(SET_INDICES,2,indices));
    /* Two stream bindings share one internal resource lifetime. */
    stream[1]=1;assert(!call(SET_STREAM,5,stream));assert(!release(vb) && !release(ib));
    uint32_t get_stream[]={device_at,0,GUEST_OUT,GUEST_OUT+4,GUEST_OUT+8};assert(!call(GET_STREAM,5,get_stream) && get(GUEST_OUT)==vb && get(GUEST_OUT+4)==0 && get(GUEST_OUT+8)==20);assert(!release(vb));
    IDirect3DVertexBuffer9 *actual_vb=NULL;UINT offset=0,stride=0;assert(SUCCEEDED(IDirect3DDevice9_GetStreamSource(device,0,&actual_vb,&offset,&stride)) && (uintptr_t)actual_vb==native_vb && !offset && stride==20);IDirect3DVertexBuffer9_Release(actual_vb);
    uint32_t get_index[]={device_at,GUEST_OUT};assert(!call(GET_INDICES,2,get_index) && get(GUEST_OUT)==ib);assert(!release(ib));
    IDirect3DIndexBuffer9 *actual_ib=NULL;assert(SUCCEEDED(IDirect3DDevice9_GetIndices(device,&actual_ib)) && (uintptr_t)actual_ib==native_ib);IDirect3DIndexBuffer9_Release(actual_ib);
    uint32_t frequency_args[]={device_at,0,D3DSTREAMSOURCE_INDEXEDDATA|3};assert(!call(SET_FREQUENCY,3,frequency_args));
    frequency_args[2]=GUEST_OUT;assert(!call(GET_FREQUENCY,3,frequency_args));UINT native_frequency=0;assert(SUCCEEDED(IDirect3DDevice9_GetStreamSourceFreq(device,0,&native_frequency)) && get(GUEST_OUT)==native_frequency);
    frequency_args[2]=0;HRESULT invalid=IDirect3DDevice9_SetStreamSourceFreq(device,0,0);assert(call(SET_FREQUENCY,3,frequency_args)==(uint32_t)invalid);
    stream[0]=device_at;stream[1]=0;stream[2]=0;stream[3]=stream[4]=0;fail_bind=1;assert(call(SET_STREAM,5,stream)==0x80004005);fail_bind=0;
    assert(!call(GET_STREAM,5,get_stream) && get(GUEST_OUT)==vb);assert(!release(vb));
    assert(!call(SET_STREAM,5,stream));stream[1]=1;assert(!call(SET_STREAM,5,stream));indices[1]=0;assert(!call(SET_INDICES,2,indices));
    get_stream[1]=16;assert(call(GET_STREAM,5,get_stream)==FNV_D3D9_INVALIDCALL);
}
static void shadow_recycling(void) {
    for(unsigned n=0;n<256;n++){uint32_t resource=buffer(n&1,16+(n*113)%4096,0,D3DPOOL_SYSTEMMEM);assert(!release(resource));}
    uint32_t a=buffer(0,1u<<20,0,D3DPOOL_SYSTEMMEM),b=buffer(0,1u<<20,0,D3DPOOL_SYSTEMMEM),c=buffer(0,1u<<20,0,D3DPOOL_SYSTEMMEM);assert(!release(b));
    uint32_t args[]={device_at,2u<<20,0,0,D3DPOOL_SYSTEMMEM,GUEST_OUT,0};put(GUEST_OUT,123);assert(call(CREATE_VB,7,args)==FNV_D3D9_OUTOFMEMORY && !get(GUEST_OUT));
    assert(!release(c));args[1]=3u<<20;assert(!call(CREATE_VB,7,args));assert(!release(get(GUEST_OUT)) && !release(a));
    fail_buffer=1;args[1]=64;assert(call(CREATE_VB,7,args)==0x80004005 && !get(GUEST_OUT));fail_buffer=0;
    uint32_t whole=buffer(0,shadow_bytes,0,D3DPOOL_SYSTEMMEM);assert(!release(whole));
}
static void declaration_checks(uint32_t wrong_kind) {
    for(unsigned type=0;type<17;type++) {
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
            {1,4,(BYTE)type,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9 *oracle=NULL;HRESULT expected=IDirect3DDevice9_CreateVertexDeclaration(device,elements,&oracle);
        memcpy(ptr(DATA),elements,sizeof(elements));uint32_t args[]={device_at,DATA,GUEST_OUT};
        uint32_t result=call(CREATE_DECL,3,args);assert(result==(uint32_t)expected);
        if(FAILED(expected))continue;
        uint32_t decl=get(GUEST_OUT);uintptr_t native=last_declaration;
        D3DVERTEXELEMENT9 readback[65];memset(readback,0xAB,sizeof(readback));UINT count=0;
        assert(SUCCEEDED(IDirect3DVertexDeclaration9_GetDeclaration(oracle,readback,&count)) && count==3);
        memset(ptr(DATA),0xAB,sizeof(readback));put(GUEST_OUT,0);uint32_t query[]={decl,DATA,GUEST_OUT};
        assert(!call(GET_ELEMENTS,3,query) && get(GUEST_OUT)==count && !memcmp(ptr(DATA),readback,sizeof(readback)));
        query[1]=0;put(GUEST_OUT,123);assert(!call(GET_ELEMENTS,3,query) && get(GUEST_OUT)==count);
        memcpy(ptr(DATA),&IID_IDirect3DVertexDeclaration9,16);uint32_t qi[]={decl,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==decl);assert(release(decl)==1);
        memcpy(ptr(DATA),&IID_IDirect3DResource9,16);assert(call(QUERY,3,qi)==(uint32_t)E_NOINTERFACE && !get(GUEST_OUT));
        uint32_t parent[]={decl,GUEST_OUT};assert(!call(GET_DEVICE,2,parent) && get(GUEST_OUT)==device_at);release(device_at);
        uint32_t binding[]={device_at,decl};assert(!call(SET_DECL,2,binding));assert(!release(decl));
        IDirect3DVertexDeclaration9 *bound=NULL;assert(SUCCEEDED(IDirect3DDevice9_GetVertexDeclaration(device,&bound)) && (uintptr_t)bound==native);IDirect3DVertexDeclaration9_Release(bound);
        binding[1]=GUEST_OUT;assert(!call(GET_DECL,2,binding) && get(GUEST_OUT)==decl);assert(!release(decl));
        binding[1]=0;fail_bind=1;assert(call(SET_DECL,2,binding)==0x80004005);fail_bind=0;
        binding[1]=GUEST_OUT;assert(!call(GET_DECL,2,binding) && get(GUEST_OUT)==decl);assert(!release(decl));
        binding[1]=wrong_kind;assert(call(SET_DECL,2,binding)==FNV_D3D9_INVALIDCALL);
        binding[1]=0;assert(!call(SET_DECL,2,binding));binding[1]=GUEST_OUT;assert(!call(GET_DECL,2,binding) && !get(GUEST_OUT));
        IDirect3DVertexDeclaration9_Release(oracle);declaration_roundtrips++;
    }
    assert(declaration_roundtrips);
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    uint32_t args[]={device_at,DATA,GUEST_OUT};memcpy(ptr(DATA),elements,sizeof(elements));
    fail_buffer=1;assert(call(CREATE_DECL,3,args)==0x80004005 && !get(GUEST_OUT));fail_buffer=0;
    for(unsigned i=0;i<256;i++) {
        memcpy(ptr(DATA),elements,sizeof(elements));assert(!call(CREATE_DECL,3,args));uint32_t first=get(GUEST_OUT);
        assert(!call(CREATE_DECL,3,args));uint32_t second=get(GUEST_OUT);
        if(first==second)assert(release(second)==1);else assert(!release(second));
        assert(!release(first));
    }
    unsigned before=declaration_creates;memset(ptr(DATA),0,65*8);
    assert(call(CREATE_DECL,3,args)==FNV_D3D9_INVALIDCALL && !get(GUEST_OUT) && declaration_creates==before);
    args[1]=BASE+ARENA-4;assert(call(CREATE_DECL,3,args)==FNV_D3D9_INVALIDCALL && declaration_creates==before);
}
static unsigned char *read_bytes(const char *path,uint32_t *size) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *bytes=malloc((size_t)n);assert(bytes && fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(uint32_t)n;return bytes;
}
static HWND setup_geometry(char **argv) {
    FILE *f=fopen(argv[2],"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9SHD1",8));catalog_count=read_file32(f);assert(catalog_count && catalog_count<=8192);
    catalog=calloc(catalog_count,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<catalog_count;i++){uint32_t stage=read_file32(f),size=read_file32(f);assert(stage<2 && size>=8 && size<=1048576);void *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);catalog[i]=(struct fnv_d3d9_shader_code){bytes,size};}assert(fgetc(f)==EOF);fclose(f);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config cfg={0};cfg.memory=arena;cfg.guest_base=BASE;cfg.memory_bytes=ARENA;cfg.code_low=BASE+0x1000;cfg.code_high=BASE+0x2000;assert(fnv_cpu_create(&cfg,&cpu)==FNV_CPU_OK);registry_checks();
    struct fnv_vm_config mc={ptr(POOL),POOL,8u<<20,8u<<20,&permissions,fnv_vm_eager_apply};assert(!fnv_vm_create(&mc,&vm).error);
    uint32_t objects=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(objects);code_at=fnv_vm_allocate(vm,0,1048576,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(code_at);
    shadow_base=fnv_vm_allocate(vm,0,shadow_bytes,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(shadow_base);
    HWND window=CreateWindowExA(0,"STATIC","FNV buffer oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=objects,.objects_bytes=65536,.resource_capacity=4096,.vertex_floats=256,.pixel_floats=224,.catalog=catalog,.catalog_count=catalog_count,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,.device_release=device_release,.bind_shader=set_shader,
        .create_buffer=create_native_buffer,.update_buffer=update_native_buffer,.bind_stream=set_stream,.bind_indices=set_indices,.stream_frequency=set_frequency,.create_declaration=create_native_declaration,.bind_declaration=set_declaration},
        .memory=vm,.buffer_address=shadow_base,.buffer_bytes=shadow_bytes};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);return window;
}
int main(int argc,char **argv) {
    assert(argc==6);
    if(strcmp(argv[3],"normal") && strcmp(argv[3],"nested") && strcmp(argv[3],"bound_parent") && strcmp(argv[3],"decl_parent"))return baseline_main(4,argv);
    HWND window=setup_geometry(argv);uint32_t vb_size,ib_size;unsigned char *vb_data=read_bytes(argv[4],&vb_size),*ib_data=read_bytes(argv[5],&ib_size);
    uint32_t vb=buffer(0,vb_size,D3DUSAGE_DYNAMIC,D3DPOOL_DEFAULT);uintptr_t native_vb=last_buffer;
    uint32_t ib=buffer(1,ib_size,D3DUSAGE_DYNAMIC,D3DPOOL_DEFAULT);uintptr_t native_ib=last_buffer;
    if(!strcmp(argv[3],"nested")){uint32_t at=lock_buffer(vb,0,0,0);(void)at;uint32_t args[]={vb,0,0,GUEST_OUT,0};struct fnv_cpu_fault fault;
        assert(invoke(LOCK_BUFFER,5,args,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);}
    else if(!strcmp(argv[3],"decl_parent")) {
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        memcpy(ptr(DATA),elements,sizeof(elements));uint32_t args[]={device_at,DATA,GUEST_OUT};assert(!call(CREATE_DECL,3,args));uint32_t decl=get(GUEST_OUT);
        uint32_t binding[]={device_at,decl};assert(!call(SET_DECL,2,binding));assert(!release(decl));assert(!release(vb) && !release(ib));assert(!release(device_at));
    }
    else if(!strcmp(argv[3],"bound_parent")){uint32_t args[]={device_at,0,vb,0,20};assert(!call(SET_STREAM,5,args));assert(!release(vb) && !release(ib));assert(!release(device_at));}
    else {
        descriptions(vb,native_vb,0);descriptions(ib,native_ib,1);
        uint32_t mapped=lock_buffer(vb,0,0,D3DLOCK_DISCARD);memcpy(ptr(mapped),vb_data,vb_size);unlock_buffer(vb);check_bytes(native_vb,vb_data,vb_size);
        mapped=lock_buffer(ib,0,0,D3DLOCK_DISCARD);memcpy(ptr(mapped),ib_data,ib_size);unlock_buffer(ib);check_bytes(native_ib,ib_data,ib_size);
        unsigned before=updates;mapped=lock_buffer(vb,0,0,D3DLOCK_READONLY);assert(!memcmp(ptr(mapped),vb_data,vb_size));unlock_buffer(vb);assert(updates==before);
        mapped=lock_buffer(vb,0,16,D3DLOCK_NOOVERWRITE);memset(ptr(mapped),0x37,16);memset(vb_data,0x37,16);fail_update=1;assert(call(UNLOCK_BUFFER,1,&vb)==0x80004005);fail_update=0;unlock_buffer(vb);check_bytes(native_vb,vb_data,vb_size);
        uint32_t invalid_lock[]={vb,vb_size,1,GUEST_OUT,0};assert(call(LOCK_BUFFER,5,invalid_lock)==FNV_D3D9_INVALIDCALL);
        declaration_checks(vb);shader_bindings();buffer_bindings(vb,ib,native_vb,native_ib);shadow_recycling();
        uint32_t draw[]={device_at,D3DPT_TRIANGLELIST,0,0,4,0,2};struct fnv_cpu_fault fault;assert(invoke(DRAW_PENDING,7,draw,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK);assert(fnv_d3d9_com_destroy(com)==FNV_CPU_OK);assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(!fnv_vm_destroy(vm).error);assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    for(unsigned i=0;i<catalog_count;i++)free((void*)catalog[i].bytes);free(catalog);free(vb_data);free(ib_data);
    printf("{\"status\":\"PASS\",\"prepared_methods\":55,\"buffer_creates\":%u,\"buffer_updates\":%u,\"bind_calls\":%u,\"captured_vertex_bytes\":%u,\"captured_index_bytes\":%u,\"declaration_creates\":%u,\"declaration_roundtrips\":%u,\"gpu_rendered\":false}\n",buffer_creates,updates,binds,vb_size,ib_size,declaration_creates,declaration_roundtrips);return 0;
}
