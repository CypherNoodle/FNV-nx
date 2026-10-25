struct fnv_native_image;
struct fnv_d3d9_shader_backend;
static void cube_backend(struct fnv_d3d9_shader_backend *);
static void bind_cube_import(struct fnv_native_image *);
#define FNV_TEXTURE_BACKEND_SETUP cube_backend
#define FNV_TEXTURE_IMPORT_SETUP bind_cube_import
#define FNV_TEXTURE_BASELINE_MAIN fnv_texture_baseline_main
#include "d3d9_textures_host.c"
#undef main
#include "dds_image_info.h"
static uint32_t cube_call(unsigned method,unsigned argc,const uint32_t *args){
    uint32_t value=0;struct fnv_cpu_fault fault={0};enum fnv_cpu_result r=invoke(method,argc,args,&value,&fault);
    if(r!=FNV_CPU_OK)fprintf(stderr,"cube method=%u result=%u fault=%s at=%08x\n",method,r,fault.text,fault.address);
    assert(r==FNV_CPU_OK);return value;
}
#define call cube_call

_Static_assert(offsetof(IDirect3DDevice9Vtbl,CreateCubeTexture)==25*sizeof(void*),"cube create ABI");
_Static_assert(offsetof(IDirect3DCubeTexture9Vtbl,LockRect)==19*sizeof(void*),"cube lock ABI");
static unsigned fail_face=UINT32_MAX,updates;
static uint32_t cube_create(void *ctx,uint32_t edge,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    (void)ctx;IDirect3DCubeTexture9 *cube=NULL;HRESULT hr=IDirect3DDevice9_CreateCubeTexture(device,edge,levels,usage,format,pool,&cube,NULL);
    if(SUCCEEDED(hr))last_texture=*out=(uintptr_t)cube;return (uint32_t)hr;
}
static uint32_t cube_upload(void *ctx,uintptr_t id,uint32_t face,uint32_t level,uint32_t left,uint32_t top,uint32_t width,uint32_t height,uint32_t flags,uint32_t pitch,const void *data){
    (void)ctx;if(face==fail_face && level==1)return 0x80004005;
    IDirect3DCubeTexture9 *cube=(IDirect3DCubeTexture9*)id;D3DSURFACE_DESC desc;D3DLOCKED_RECT mapped;
    assert(SUCCEEDED(IDirect3DCubeTexture9_GetLevelDesc(cube,level,&desc)));
    RECT rect={(LONG)left,(LONG)top,(LONG)(left+width),(LONG)(top+height)};
    HRESULT hr=IDirect3DCubeTexture9_LockRect(cube,face,level,&mapped,&rect,flags);if(FAILED(hr))return (uint32_t)hr;
    unsigned block,unit;format_shape(desc.Format,&block,&unit);unsigned row=(width+block-1)/block*unit,rows=(height+block-1)/block;
    for(unsigned y=0;y<rows;y++)memcpy((char*)mapped.pBits+y*mapped.Pitch,(const char*)data+y*pitch,row);
    hr=IDirect3DCubeTexture9_UnlockRect(cube,face,level);if(SUCCEEDED(hr))updates++;return (uint32_t)hr;
}
static void cube_backend(struct fnv_d3d9_shader_backend *b){b->create_cube_texture=cube_create;b->upload_cube_texture=cube_upload;}
static void bind_cube_import(struct fnv_native_image *image){
    size_t n,bound;const struct fnv_cpu_export *e=fnv_d3d9_com_exports(com,&n);assert(n==2);
    struct fnv_native_import imports[4];
    assert(fnv_cpu_bind_imports(cpu,image,imports,4,e,n,&bound)==FNV_NATIVE_OK && bound==1);
}
static unsigned char dds[65536];
static unsigned make_cube(unsigned format,unsigned edge,unsigned *levels){
    memset(dds,0,sizeof(dds));*levels=1;for(unsigned n=edge;n>1;n>>=1)(*levels)++;
    const uint32_t fields[][2]={{0,0x20534444},{4,124},{8,0x21007},{12,edge},{16,edge},{28,*levels},{76,32},{108,0x401008},{112,0xfe00}};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++)fnv_dds_put32(dds+fields[i][0],fields[i][1]);
    if(format==D3DFMT_DXT1){fnv_dds_put32(dds+80,4);fnv_dds_put32(dds+84,format);}else{
        fnv_dds_put32(dds+80,0x41);fnv_dds_put32(dds+88,32);fnv_dds_put32(dds+92,0xff0000);fnv_dds_put32(dds+96,0xff00);fnv_dds_put32(dds+100,0xff);fnv_dds_put32(dds+104,0xff000000);}
    unsigned offset=128,block,unit;format_shape(format,&block,&unit);
    for(unsigned face=0;face<6;face++)for(unsigned l=0,w=edge;l<*levels;l++,w=w>1?w/2:1){unsigned bytes=((w+block-1)/block)*unit*((w+block-1)/block);
        assert(offset+bytes<=sizeof(dds));for(unsigned i=0;i<bytes;i++)dds[offset+i]=(unsigned char)(face*31+l*17+i);offset+=bytes;}
    return offset;
}
int main(int argc,char **argv){
    assert(argc==4);HWND window=setup(argv);assert(!strcmp(argv[3],"normal"));
    HMODULE dll=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(dll);
    typedef HRESULT(WINAPI *factory)(IDirect3DDevice9*,const void*,UINT,IDirect3DCubeTexture9**);
    FARPROC proc=GetProcAddress(dll,"D3DXCreateCubeTextureFromFileInMemory");factory create;memcpy(&create,&proc,sizeof(create));assert(create);
    unsigned checks=0,tail=0;
    const unsigned formats[]={21,D3DFMT_DXT1};
    for(unsigned mode=0;mode<2;mode++){
        unsigned levels,edge=mode?32:8,format=formats[mode],bytes=make_cube(format,edge,&levels);memcpy(ptr(code_at),dds,bytes);
        uint32_t args[]={device_at,code_at,bytes,GUEST_OUT};IDirect3DCubeTexture9 *oracle=NULL;assert(SUCCEEDED(create(device,dds,bytes,&oracle)));
        assert(!call(28,4,args));uint32_t guest=get(GUEST_OUT);IDirect3DCubeTexture9 *actual=(IDirect3DCubeTexture9*)last_texture;
        assert(call(RESOURCE_TYPE,1,&guest)==5 && call(LEVELS,1,&guest)==levels);
        memcpy(ptr(DATA),&IID_IDirect3DCubeTexture9,16);uint32_t qi[]={guest,DATA,GUEST_OUT};assert(!call(QUERY,3,qi)&&get(GUEST_OUT)==guest);call(RELEASE,1,&guest);
        for(unsigned face=0;face<6;face++)for(unsigned l=0;l<levels;l++){
            D3DSURFACE_DESC a,b;assert(SUCCEEDED(IDirect3DCubeTexture9_GetLevelDesc(actual,l,&a)) && SUCCEEDED(IDirect3DCubeTexture9_GetLevelDesc(oracle,l,&b)) && !memcmp(&a,&b,sizeof(a)));
            uint32_t desc[]={guest,l,DATA};assert(!call(LEVEL_DESC,3,desc)&&!memcmp(ptr(DATA),&a,sizeof(a)));
            uint32_t lock[]={guest,face,l,GUEST_OUT,0,D3DLOCK_READONLY};assert(!call(LOCK_RECT,6,lock));
            D3DLOCKED_RECT native,reference;assert(SUCCEEDED(IDirect3DCubeTexture9_LockRect(actual,face,l,&native,NULL,D3DLOCK_READONLY)) && SUCCEEDED(IDirect3DCubeTexture9_LockRect(oracle,face,l,&reference,NULL,D3DLOCK_READONLY)));
            unsigned block,unit;format_shape(format,&block,&unit);unsigned row=(a.Width+block-1)/block*unit,rows=(a.Height+block-1)/block;
            for(unsigned y=0;y<rows;y++){
                assert(!memcmp(ptr(get(GUEST_OUT+4))+y*get(GUEST_OUT),(char*)native.pBits+y*native.Pitch,row));
                if(memcmp((char*)native.pBits+y*native.Pitch,(char*)reference.pBits+y*reference.Pitch,row)){assert(mode && a.Width<4);tail++;}
            }
            assert(SUCCEEDED(IDirect3DCubeTexture9_UnlockRect(actual,face,l)) && SUCCEEDED(IDirect3DCubeTexture9_UnlockRect(oracle,face,l)));
            uint32_t unlock[]={guest,face,l};assert(!call(UNLOCK_RECT,3,unlock));checks++;
        }
        uint32_t bad[]={guest,6,0,GUEST_OUT,0,0};assert(call(LOCK_RECT,6,bad)==FNV_D3D9_INVALIDCALL);
        uint32_t set[]={device_at,0,guest};assert(!call(SET_TEXTURE,3,set));uint32_t getargs[]={device_at,0,GUEST_OUT};assert(!call(GET_TEXTURE,3,getargs)&&get(GUEST_OUT)==guest);call(RELEASE,1,&guest);set[2]=0;assert(!call(SET_TEXTURE,3,set));call(RELEASE,1,&guest);IDirect3DCubeTexture9_Release(oracle);
        fail_face=3;memcpy(ptr(code_at),dds,bytes);put(GUEST_OUT,0x12345678);assert(call(28,4,args)==0x80004005 && get(GUEST_OUT)==0x12345678);fail_face=UINT32_MAX;
    }
    FreeLibrary(dll);assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK && fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK);
    assert(!fnv_vm_destroy(vm).error && VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);for(unsigned i=0;i<catalog_count;i++)free((void*)catalog[i].bytes);free(catalog);
    printf("{\"status\":\"PASS\",\"cube_face_mip_checks\":%u,\"native_compressed_tail_differences\":%u,\"uploads\":%u,\"game_started\":false}\n",checks,tail,updates);return 0;
}
