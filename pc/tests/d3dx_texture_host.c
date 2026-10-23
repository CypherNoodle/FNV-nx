struct fnv_native_image;
static void bind_dds_import(struct fnv_native_image *);
#define FNV_TEXTURE_IMPORT_SETUP bind_dds_import
#define FNV_TEXTURE_BASELINE_MAIN fnv_texture_baseline_main
#include "d3d9_textures_host.c"
#undef main
#include "dds_image_info.h"
static void bind_dds_import(struct fnv_native_image *image){
    size_t n,bound;const struct fnv_cpu_export *e=fnv_d3d9_com_exports(com,&n);struct fnv_native_import imports[4];
    assert(n==1 && !strcmp(e[0].name,"D3DXCreateTextureFromFileInMemory"));
    assert(fnv_cpu_bind_imports(cpu,image,imports,4,e,n,&bound)==FNV_NATIVE_OK && bound==1);
}
static unsigned char image_data[262144];
static uint32_t make_dds(unsigned width,unsigned height,unsigned format,unsigned *levels){
    memset(image_data,0,sizeof(image_data));*levels=1;for(unsigned n=width>height?width:height;n>1;n>>=1)(*levels)++;
    const uint32_t fields[][2]={{0,0x20534444},{4,124},{8,0x21007},{12,height},{16,width},{28,*levels},{76,32},{108,0x401008}};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i)fnv_dds_put32(image_data+fields[i][0],fields[i][1]);
    if(format>=D3DFMT_DXT1){fnv_dds_put32(image_data+80,4);fnv_dds_put32(image_data+84,format);}
    else if(format==21 || format==22){fnv_dds_put32(image_data+80,format==21?0x41:0x40);fnv_dds_put32(image_data+88,32);
        fnv_dds_put32(image_data+92,0xff0000);fnv_dds_put32(image_data+96,0xff00);fnv_dds_put32(image_data+100,0xff);fnv_dds_put32(image_data+104,format==21?0xff000000:0);}
    else if(format==23){fnv_dds_put32(image_data+80,0x40);fnv_dds_put32(image_data+88,16);fnv_dds_put32(image_data+92,0xf800);fnv_dds_put32(image_data+96,0x7e0);fnv_dds_put32(image_data+100,0x1f);}
    else if(format==28){fnv_dds_put32(image_data+80,2);fnv_dds_put32(image_data+88,8);fnv_dds_put32(image_data+104,0xff);}
    else {assert(format==50);fnv_dds_put32(image_data+80,0x20000);fnv_dds_put32(image_data+88,8);fnv_dds_put32(image_data+92,0xff);}
    unsigned block,unit;format_shape(format,&block,&unit);uint32_t bytes=128;
    for(unsigned level=0;level<*levels;++level){unsigned size=((width+block-1)/block)*unit*((height+block-1)/block);assert(bytes+size<sizeof(image_data));
        for(unsigned i=0;i<size;++i)image_data[bytes+i]=(unsigned char)(i*17+level*31+format);
        bytes+=size;width=width>1?width/2:1;height=height>1?height/2:1;}
    return bytes;
}
static ULONG device_refs(void){ULONG n=IDirect3DDevice9_AddRef(device);IDirect3DDevice9_Release(device);return n-1;}
typedef HRESULT(WINAPI *surface_copy_fn)(IDirect3DSurface9 *,const PALETTEENTRY *,const RECT *,IDirect3DSurface9 *,const PALETTEENTRY *,const RECT *,DWORD,D3DCOLOR);
static surface_copy_fn copy_surface;
static int compare_pixels(IDirect3DTexture9 *actual,IDirect3DTexture9 *oracle,unsigned level,unsigned width,unsigned height){
    IDirect3DSurface9 *a,*b,*x,*y;assert(SUCCEEDED(IDirect3DTexture9_GetSurfaceLevel(actual,level,&a)) && SUCCEEDED(IDirect3DTexture9_GetSurfaceLevel(oracle,level,&b)));
    assert(SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&x,NULL)) &&
           SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&y,NULL)));
    assert(SUCCEEDED(copy_surface(x,NULL,NULL,a,NULL,NULL,1,0)) && SUCCEEDED(copy_surface(y,NULL,NULL,b,NULL,NULL,1,0)));
    D3DLOCKED_RECT lx,ly;assert(SUCCEEDED(IDirect3DSurface9_LockRect(x,&lx,NULL,D3DLOCK_READONLY)) && SUCCEEDED(IDirect3DSurface9_LockRect(y,&ly,NULL,D3DLOCK_READONLY)));
    int equal=1;
    for(unsigned row=0;row<height;++row)if(memcmp((char*)lx.pBits+row*lx.Pitch,(char*)ly.pBits+row*ly.Pitch,width*4))equal=0;
    assert(SUCCEEDED(IDirect3DSurface9_UnlockRect(x)) && SUCCEEDED(IDirect3DSurface9_UnlockRect(y)));
    IDirect3DSurface9_Release(a);IDirect3DSurface9_Release(b);IDirect3DSurface9_Release(x);IDirect3DSurface9_Release(y);return equal;
}
int main(int argc,char **argv){
    assert(argc==4);HWND window=setup(argv);unsigned levels,checks=0,rollbacks=0,source_checks=0,tail_differences=0,generated_checks=0,generation_rollbacks=0;
    uint32_t bytes=make_dds(8,8,D3DFMT_DXT1,&levels);memcpy(ptr(code_at),image_data,bytes);
    uint32_t args[]={device_at,code_at,bytes,GUEST_OUT};
    if(strcmp(argv[3],"normal")){if(!strcmp(argv[3],"dds_shape"))put(code_at+16,7);else {bytes=make_dds(8,8,D3DFMT_DXT2,&levels);memcpy(ptr(code_at),image_data,bytes);put(code_at+28,1);args[2]=bytes;}
        struct fnv_cpu_fault f;assert(invoke(28,4,args,NULL,&f)==FNV_CPU_GUEST_FAULT && f.kind==FNV_CPU_UNIMPLEMENTED);
    }else {
        HMODULE lib=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(lib);
        typedef HRESULT(WINAPI *create_fn)(IDirect3DDevice9 *,const void *,UINT,IDirect3DTexture9 **);FARPROC proc=GetProcAddress(lib,"D3DXCreateTextureFromFileInMemory");create_fn create;memcpy(&create,&proc,sizeof(create));assert(create);proc=GetProcAddress(lib,"D3DXLoadSurfaceFromSurface");memcpy(&copy_surface,&proc,sizeof(copy_surface));assert(copy_surface);
        const uint32_t formats[]={21,22,23,28,50,D3DFMT_DXT1,D3DFMT_DXT3,D3DFMT_DXT5};
        for(unsigned n=0;n<160;++n){unsigned width=1u<<(2+n%5),height=1u<<(2+n/5%5),format=formats[n%8];bytes=make_dds(width,height,format,&levels);
            memcpy(ptr(code_at),image_data,bytes);args[2]=bytes;args[3]=n%3==0?code_at+17:GUEST_OUT+1;
            IDirect3DTexture9 *oracle=NULL;HRESULT wanted=create(device,image_data,bytes,&oracle);assert(SUCCEEDED(wanted) && oracle);
            assert(call(28,4,args)==(uint32_t)wanted);uint32_t texture=get(args[3]);IDirect3DTexture9 *actual=(IDirect3DTexture9*)last_texture;
            assert(texture && IDirect3DTexture9_GetLevelCount(actual)==IDirect3DTexture9_GetLevelCount(oracle));
            uint32_t offset=128;
            for(unsigned level=0;level<levels;++level){D3DSURFACE_DESC a,b;assert(SUCCEEDED(IDirect3DTexture9_GetLevelDesc(actual,level,&a)) && SUCCEEDED(IDirect3DTexture9_GetLevelDesc(oracle,level,&b)) && !memcmp(&a,&b,sizeof(a)));
                unsigned block,unit;format_shape(format,&block,&unit);unsigned row=(a.Width+block-1)/block*unit,rows=(a.Height+block-1)/block;
                D3DLOCKED_RECT raw;assert(SUCCEEDED(IDirect3DTexture9_LockRect(actual,level,&raw,NULL,D3DLOCK_READONLY)));
                for(unsigned y=0;y<rows;++y)assert(!memcmp((char*)raw.pBits+y*raw.Pitch,image_data+offset+y*row,row));
                assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(actual,level)));offset+=row*rows;source_checks++;
                int equal=compare_pixels(actual,oracle,level,a.Width,a.Height);
                if(!equal){assert(block==4 && (a.Width<4 || a.Height<4));tail_differences++;}else checks++;
            }
            assert(!release(texture));IDirect3DTexture9_Release(oracle);
        }
        /* Missing BC3 levels: flat red/alpha137 has an independent exact
         * pixel expectation, and can also be compared with native D3DX. */
        for(unsigned sample=0;sample<36;++sample){unsigned format=(sample%3==0?D3DFMT_DXT1:sample%3==1?D3DFMT_DXT3:D3DFMT_DXT5),unit=format==D3DFMT_DXT1?8:16;unsigned width=1u<<(2+sample%5),height=1u<<(2+sample/3%4),supplied=1+sample%2;
            bytes=make_dds(width,height,format,&levels);fnv_dds_put32(image_data+28,supplied);
            uint32_t provided_bytes=128;for(unsigned level=0,w=width,h=height;level<supplied;++level){unsigned size=((w+3)/4)*((h+3)/4)*unit;
                for(unsigned i=0;i<size;i+=unit){unsigned char *block=image_data+provided_bytes+i;memset(block,0,unit);
                    if(format==D3DFMT_DXT1)block[1]=block[3]=0xf8;
                    else {if(format==D3DFMT_DXT3)memset(block,0x88,8);else block[0]=block[1]=137;block[9]=block[11]=0xf8;}}
                provided_bytes+=size;w=w>1?w/2:1;h=h>1?h/2:1;}
            memcpy(ptr(code_at),image_data,provided_bytes);args[1]=code_at;args[2]=provided_bytes;args[3]=GUEST_OUT;
            IDirect3DTexture9 *oracle=NULL;assert(SUCCEEDED(create(device,image_data,provided_bytes,&oracle)) && !call(28,4,args));
            uint32_t texture=get(GUEST_OUT);IDirect3DTexture9 *actual=(IDirect3DTexture9*)last_texture;
            assert(IDirect3DTexture9_GetLevelCount(actual)==levels && IDirect3DTexture9_GetLevelCount(oracle)==levels);
            for(unsigned level=0;level<levels;++level){D3DSURFACE_DESC desc;assert(SUCCEEDED(IDirect3DTexture9_GetLevelDesc(actual,level,&desc)));
                assert(compare_pixels(actual,oracle,level,desc.Width,desc.Height));generated_checks++;}
            assert(!release(texture));IDirect3DTexture9_Release(oracle);
            if(!sample){ULONG before=device_refs();for(unsigned level=0;level<levels;++level){fail_texture_level=level;put(GUEST_OUT,0xdeadbeef);unsigned releases=deleted;
                assert(call(28,4,args)==0x80004005 && get(GUEST_OUT)==0xdeadbeef && deleted==releases+1 && device_refs()==before);generation_rollbacks++;}
                fail_texture_level=UINT32_MAX;}
        }
        bytes=make_dds(64,64,21,&levels);memcpy(ptr(code_at),image_data,bytes);args[2]=bytes;args[3]=GUEST_OUT;
        ULONG refs=device_refs();unsigned deleted_before=deleted;
        for(unsigned n=0;n<140;++n){fail_texture_level=n%levels;put(GUEST_OUT,0xa5a5a5a5);unsigned destroyed=deleted;
            assert(call(28,4,args)==0x80004005 && get(GUEST_OUT)==0xa5a5a5a5 && deleted==destroyed+1 && device_refs()==refs);rollbacks++;}
        fail_texture_level=UINT32_MAX;failed_texture=1;assert(call(28,4,args)==0x80004005 && get(GUEST_OUT)==0xa5a5a5a5 && device_refs()==refs);failed_texture=0;
        assert(deleted==deleted_before+140);assert(!call(28,4,args));assert(!release(get(GUEST_OUT)) && device_refs()==refs);
        args[2]=127;put(GUEST_OUT,0xa5a5a5a5);assert(call(28,4,args)==FNV_DDS_INVALID_DATA && get(GUEST_OUT)==0xa5a5a5a5);
        args[1]=0;assert(call(28,4,args)==FNV_D3D9_INVALIDCALL && get(GUEST_OUT)==0xa5a5a5a5);
        FreeLibrary(lib);
    }
    assert(fnv_cpu_detach()==FNV_CPU_OK && fnv_d3d9_com_destroy(com)==FNV_CPU_OK);
    assert(fnv_cpu_unregister_native(cpu,dummy_token,1)==FNV_CPU_OK && fnv_cpu_destroy(cpu)==FNV_CPU_OK && !fnv_vm_destroy(vm).error);
    assert(VirtualFree(arena,0,MEM_RELEASE));IDirect3D9_Release(api);DestroyWindow(window);
    for(unsigned i=0;i<catalog_count;i++)free((void*)catalog[i].bytes);free(catalog);
    printf("{\"status\":\"PASS\",\"native_mip_pixel_agreements\":%u,\"native_compressed_tail_differences\":%u,\"exact_dds_mip_copies\":%u,\"upload_rollbacks\":%u,\"generated_mip_pixel_checks\":%u,\"generation_rollbacks\":%u,\"gpu_rendered\":false}\n",checks,tail_differences,source_checks,rollbacks,generated_checks,generation_rollbacks);return 0;
}
