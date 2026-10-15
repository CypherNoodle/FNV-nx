#define main baseline_main
#include "d3d9_com_host.c"
#undef main
enum {CREATE_TEXTURE=20,LEVELS=21,LEVEL_DESC=22,LOCK_RECT=23,UNLOCK_RECT=24,SET_TEXTURE=25,GET_TEXTURE=26,RESOURCE_TYPE=27};
_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateTexture)==23*sizeof(void*),"CreateTexture slot");
_Static_assert(offsetof(IDirect3DDevice9Vtbl,SetTexture)==65*sizeof(void*),"SetTexture slot");
_Static_assert(offsetof(IDirect3DTexture9Vtbl,GetLevelCount)==13*sizeof(void*),"LevelCount slot");
_Static_assert(offsetof(IDirect3DTexture9Vtbl,GetLevelDesc)==17*sizeof(void*),"LevelDesc slot");
_Static_assert(offsetof(IDirect3DTexture9Vtbl,LockRect)==19*sizeof(void*) && sizeof(D3DSURFACE_DESC)==32,"texture ABI");
static unsigned texture_creates,texture_updates,texture_roundtrips,mip_roundtrips,failed_texture,failed_upload,failed_binding;
static uintptr_t last_texture;
static uint32_t create_texture(void *ctx,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out) {
    (void)ctx;if(failed_texture)return 0x80004005;IDirect3DTexture9 *t=NULL;
    HRESULT r=IDirect3DDevice9_CreateTexture(device,w,h,levels,usage,format,pool,&t,NULL);
    if(SUCCEEDED(r)){last_texture=*out=(uintptr_t)t;texture_creates++;}return (uint32_t)r;
}
static void format_shape(uint32_t format,unsigned *block,unsigned *unit) {
    *block=1;*unit=format==21 || format==22?4:format==23?2:1;
    if(format>=D3DFMT_DXT1 && format<=D3DFMT_DXT5){*block=4;*unit=format==D3DFMT_DXT1?8:16;}
}
static uint32_t upload_texture(void *ctx,uintptr_t handle,uint32_t level,uint32_t left,uint32_t top,uint32_t width,uint32_t height,uint32_t flags,uint32_t pitch,const void *data) {
    (void)ctx;if(failed_upload)return 0x80004005;IDirect3DTexture9 *t=(IDirect3DTexture9*)handle;
    D3DSURFACE_DESC desc;assert(SUCCEEDED(IDirect3DTexture9_GetLevelDesc(t,level,&desc)));
    RECT rect={(LONG)left,(LONG)top,(LONG)(left+width),(LONG)(top+height)};D3DLOCKED_RECT mapped;
    HRESULT r=IDirect3DTexture9_LockRect(t,level,&mapped,!left && !top && width==desc.Width && height==desc.Height?NULL:&rect,flags);
    if(FAILED(r))return (uint32_t)r;
    unsigned block,unit;format_shape(desc.Format,&block,&unit);unsigned bytes=((width+block-1)/block)*unit,rows=(height+block-1)/block;
    assert(mapped.Pitch>0 && (unsigned)mapped.Pitch>=bytes && pitch>=bytes);
    for(unsigned y=0;y<rows;y++)memcpy((char*)mapped.pBits+y*mapped.Pitch,(const char*)data+y*pitch,bytes);
    r=IDirect3DTexture9_UnlockRect(t,level);if(SUCCEEDED(r))texture_updates++;return (uint32_t)r;
}
static uint32_t bind_texture(void *ctx,uint32_t sampler,uintptr_t texture) {
    (void)ctx;if(failed_binding)return 0x80004005;
    return (uint32_t)IDirect3DDevice9_SetTexture(device,sampler,(IDirect3DBaseTexture9*)texture);
}
static HWND setup(char **argv) {
    /* Reuse the bounded catalog fixture; no bytes from it are rendered. */
    FILE *f=fopen(argv[2],"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9SHD1",8));catalog_count=read_file32(f);assert(catalog_count && catalog_count<=8192);
    catalog=calloc(catalog_count,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<catalog_count;i++){uint32_t stage=read_file32(f),size=read_file32(f);assert(stage<2 && size>=8 && size<=1048576);void *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);catalog[i]=(struct fnv_d3d9_shader_code){bytes,size};}assert(fgetc(f)==EOF);fclose(f);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_cpu_config cfg={0};cfg.memory=arena;cfg.guest_base=BASE;cfg.memory_bytes=ARENA;cfg.code_low=BASE+0x1000;cfg.code_high=BASE+0x2000;assert(fnv_cpu_create(&cfg,&cpu)==FNV_CPU_OK);registry_checks();
    struct fnv_vm_config mc={ptr(POOL),POOL,8u<<20,8u<<20,&permissions,fnv_vm_eager_apply};assert(!fnv_vm_create(&mc,&vm).error);
    uint32_t objects=fnv_vm_allocate(vm,0,65536,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(objects);
    code_at=fnv_vm_allocate(vm,0,1048576,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(code_at);
    uint32_t shadow=fnv_vm_allocate(vm,0,4u<<20,FNV_VM_RESERVE|FNV_VM_COMMIT,4).value;assert(shadow);
    HWND window=CreateWindowExA(0,"STATIC","FNV texture oracle",WS_POPUP,0,0,64,64,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;assert(SUCCEEDED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)));
    struct fnv_d3d9_com_config cc={.objects_address=objects,.objects_bytes=65536,.resource_capacity=4096,.vertex_floats=256,.pixel_floats=224,.catalog=catalog,.catalog_count=catalog_count,
        .backend={.create=backend_create,.addref=backend_addref,.release=backend_release,.device_addref=device_addref,.device_release=device_release,
            .create_texture=create_texture,.upload_texture=upload_texture,.bind_texture=bind_texture},.memory=vm,.buffer_address=shadow,.buffer_bytes=4u<<20};
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_OK);device_at=fnv_d3d9_com_device(com);struct fnv_cpu_state s;fnv_cpu_state_init(&s,BASE+ARENA-4096,0);assert(fnv_cpu_attach(cpu,&s)==FNV_CPU_OK);return window;
}
static uint32_t make_texture(uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool) {
    uint32_t args[]={device_at,w,h,levels,usage,format,pool,GUEST_OUT,0};assert(!call(CREATE_TEXTURE,9,args));return get(GUEST_OUT);
}
static void check_level(uint32_t texture,IDirect3DTexture9 *native,unsigned level) {
    D3DSURFACE_DESC d;assert(SUCCEEDED(IDirect3DTexture9_GetLevelDesc(native,level,&d)));
    uint32_t desc[]={texture,level,DATA};assert(!call(LEVEL_DESC,3,desc) && !memcmp(ptr(DATA),&d,sizeof(d)));
    uint32_t lock[]={texture,level,GUEST_OUT,0,0};assert(!call(LOCK_RECT,5,lock));uint32_t pitch=get(GUEST_OUT),at=get(GUEST_OUT+4);
    unsigned block,unit;format_shape(d.Format,&block,&unit);unsigned row_bytes=((d.Width+block-1)/block)*unit,rows=(d.Height+block-1)/block;
    assert(pitch>=row_bytes);for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<row_bytes;x++)ptr(at)[y*pitch+x]=(unsigned char)(level*31+y*17+x);
    uint32_t unlock[]={texture,level};failed_upload=1;assert(call(UNLOCK_RECT,2,unlock)==0x80004005);failed_upload=0;assert(!call(UNLOCK_RECT,2,unlock));
    D3DLOCKED_RECT actual;assert(SUCCEEDED(IDirect3DTexture9_LockRect(native,level,&actual,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<rows;y++)assert(!memcmp((char*)actual.pBits+y*actual.Pitch,ptr(at)+y*pitch,row_bytes));
    assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(native,level)));
    unsigned updates=texture_updates;lock[4]=D3DLOCK_READONLY;assert(!call(LOCK_RECT,5,lock));assert(!call(UNLOCK_RECT,2,unlock) && texture_updates==updates);mip_roundtrips++;
}
static void partial_upload(uint32_t texture,IDirect3DTexture9 *native) {
    D3DSURFACE_DESC desc;assert(SUCCEEDED(IDirect3DTexture9_GetLevelDesc(native,0,&desc)));
    unsigned block,unit;format_shape(desc.Format,&block,&unit);unsigned row_bytes=((desc.Width+block-1)/block)*unit,rows=(desc.Height+block-1)/block;
    unsigned char previous[256];assert(row_bytes*rows<=sizeof(previous));D3DLOCKED_RECT mapped;
    assert(SUCCEEDED(IDirect3DTexture9_LockRect(native,0,&mapped,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<rows;y++)memcpy(previous+y*row_bytes,(char*)mapped.pBits+y*mapped.Pitch,row_bytes);
    assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(native,0)));
    RECT rect=block==4?(RECT){4,4,8,8}:(RECT){2,1,5,4};memcpy(ptr(DATA),&rect,sizeof(rect));
    uint32_t lock[]={texture,0,GUEST_OUT,DATA,0};assert(!call(LOCK_RECT,5,lock));unsigned pitch=get(GUEST_OUT),at=get(GUEST_OUT+4);
    unsigned changed_rows=((rect.bottom-rect.top)+block-1)/block,changed_bytes=((rect.right-rect.left)+block-1)/block*unit;
    for(unsigned y=0;y<changed_rows;y++)memset(ptr(at)+y*pitch,0xA5,changed_bytes);
    uint32_t unlock[]={texture,0};failed_upload=1;assert(call(UNLOCK_RECT,2,unlock)==0x80004005);failed_upload=0;
    assert(SUCCEEDED(IDirect3DTexture9_LockRect(native,0,&mapped,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<rows;y++)assert(!memcmp(previous+y*row_bytes,(char*)mapped.pBits+y*mapped.Pitch,row_bytes));
    assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(native,0)));assert(!call(UNLOCK_RECT,2,unlock));
    lock[3]=0;lock[4]=D3DLOCK_READONLY;assert(!call(LOCK_RECT,5,lock));pitch=get(GUEST_OUT);at=get(GUEST_OUT+4);
    assert(SUCCEEDED(IDirect3DTexture9_LockRect(native,0,&mapped,NULL,D3DLOCK_READONLY)));
    for(unsigned y=0;y<rows;y++)assert(!memcmp(ptr(at)+y*pitch,(char*)mapped.pBits+y*mapped.Pitch,row_bytes));
    assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(native,0)));assert(!call(UNLOCK_RECT,2,unlock));
    rect.left=-1;memcpy(ptr(DATA),&rect,sizeof(rect));lock[3]=DATA;lock[4]=0;assert(call(LOCK_RECT,5,lock)==FNV_D3D9_INVALIDCALL);
    lock[1]=99;lock[3]=0;assert(call(LOCK_RECT,5,lock)==FNV_D3D9_INVALIDCALL);
}
static void bindings(uint32_t texture,uintptr_t native) {
    const GUID *ids[]={&IID_IDirect3DTexture9,&IID_IDirect3DBaseTexture9,&IID_IDirect3DResource9};
    for(unsigned i=0;i<3;i++){memcpy(ptr(DATA),ids[i],16);uint32_t qi[]={texture,DATA,GUEST_OUT};assert(!call(QUERY,3,qi) && get(GUEST_OUT)==texture);assert(release(texture)==1);}
    uint32_t parent[]={texture,GUEST_OUT};assert(!call(GET_DEVICE,2,parent) && get(GUEST_OUT)==device_at);release(device_at);
    uint32_t set[]={device_at,3,texture};assert(!call(SET_TEXTURE,3,set));assert(!release(texture));
    IDirect3DBaseTexture9 *actual=NULL;assert(SUCCEEDED(IDirect3DDevice9_GetTexture(device,3,&actual)) && (uintptr_t)actual==native);IDirect3DBaseTexture9_Release(actual);
    uint32_t get_args[]={device_at,3,GUEST_OUT};assert(!call(GET_TEXTURE,3,get_args) && get(GUEST_OUT)==texture);assert(!release(texture));
    set[2]=0;failed_binding=1;assert(call(SET_TEXTURE,3,set)==0x80004005);failed_binding=0;
    assert(!call(GET_TEXTURE,3,get_args) && get(GUEST_OUT)==texture);assert(!release(texture));
    assert(!call(SET_TEXTURE,3,set));assert(!call(GET_TEXTURE,3,get_args) && !get(GUEST_OUT));
}
int main(int argc,char **argv) {
    assert(argc==4);
    if(strcmp(argv[3],"normal") && strncmp(argv[3],"texture_",8))return baseline_main(argc,argv);
    HWND window=setup(argv);
    if(!strcmp(argv[3],"texture_format")) {
        uint32_t args[]={device_at,8,8,1,0,D3DFMT_P8,D3DPOOL_MANAGED,GUEST_OUT,0};struct fnv_cpu_fault fault;
        assert(invoke(CREATE_TEXTURE,9,args,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    } else if(!strcmp(argv[3],"texture_parent")) {
        uint32_t t=make_texture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED),args[]={device_at,0,t};assert(!call(SET_TEXTURE,3,args));assert(!release(t) && !release(device_at));
    } else if(!strcmp(argv[3],"texture_nested")) {
        uint32_t t=make_texture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED),args[]={t,0,GUEST_OUT,0,0};assert(!call(LOCK_RECT,5,args));struct fnv_cpu_fault fault;
        assert(invoke(LOCK_RECT,5,args,NULL,&fault)==FNV_CPU_GUEST_FAULT && fault.kind==FNV_CPU_UNIMPLEMENTED);
    } else {
        const D3DFORMAT formats[]={D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_R5G6B5,D3DFMT_A8,D3DFMT_L8,D3DFMT_DXT1,D3DFMT_DXT2,D3DFMT_DXT3,D3DFMT_DXT4,D3DFMT_DXT5};
        for(unsigned i=0;i<sizeof(formats)/sizeof(*formats);i++) {
            uint32_t t=make_texture(8,8,0,0,formats[i],D3DPOOL_MANAGED);IDirect3DTexture9 *native=(IDirect3DTexture9*)last_texture;
            unsigned levels=call(LEVELS,1,&t);assert(levels==IDirect3DTexture9_GetLevelCount(native) && levels==4 && call(RESOURCE_TYPE,1,&t)==D3DRTYPE_TEXTURE);
            for(unsigned l=0;l<levels;l++)check_level(t,native,l);
            partial_upload(t,native);bindings(t,(uintptr_t)native);texture_roundtrips++;
        }
        uint32_t dynamic=make_texture(8,8,0,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT);
        uint32_t lock[]={dynamic,1,GUEST_OUT,0,D3DLOCK_DISCARD};assert(call(LOCK_RECT,5,lock)==FNV_D3D9_INVALIDCALL);
        RECT full={0,0,8,8};memcpy(ptr(DATA),&full,sizeof(full));lock[1]=0;lock[3]=DATA;assert(call(LOCK_RECT,5,lock)==FNV_D3D9_INVALIDCALL);
        lock[3]=0;assert(!call(LOCK_RECT,5,lock));unsigned pitch=get(GUEST_OUT),at=get(GUEST_OUT+4);
        for(unsigned y=0;y<8;y++)memset(ptr(at)+y*pitch,0x91,32);
        uint32_t unlock[]={dynamic,0};assert(!call(UNLOCK_RECT,2,unlock));assert(!release(dynamic));
        for(unsigned i=0;i<128;i++){uint32_t t=make_texture(17,9,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED);assert(!release(t));}
        failed_texture=1;uint32_t args[]={device_at,8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,GUEST_OUT,0};assert(call(CREATE_TEXTURE,9,args)==0x80004005 && !get(GUEST_OUT));failed_texture=0;
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK && !fnv_vm_destroy(vm).error);
    assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    for(unsigned i=0;i<catalog_count;i++)free((void*)catalog[i].bytes);free(catalog);
    printf("{\"status\":\"PASS\",\"prepared_methods\":47,\"texture_creates\":%u,\"texture_updates\":%u,\"texture_roundtrips\":%u,\"mip_roundtrips\":%u}\n",texture_creates,texture_updates,texture_roundtrips,mip_roundtrips);return 0;
}
