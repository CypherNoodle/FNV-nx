#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define FNV_D3D9_OUTOFMEMORY 0x8007000eu
#include "dds_image_info.h"
#include "bc_mip_chain.inc"
void decode_block(const unsigned char *src,unsigned char *dst){fnv_bc_decode(src,0x35545844,dst);}
void decode_format(const unsigned char *src,uint32_t format,unsigned char *dst){fnv_bc_decode(src,format,dst);}
void encode_format(const unsigned char *src,uint32_t format,unsigned char *dst){fnv_bc_encode(src,format,dst);}
uint32_t complete_format(const unsigned char *src,uint32_t size,unsigned w,unsigned h,unsigned levels,unsigned maximum,uint32_t format,uint32_t budget,unsigned char **out,uint32_t *bytes){return fnv_bc_complete_dds(src,size,w,h,levels,maximum,format,budget,out,bytes);}
void half_image(unsigned char *image,unsigned w,unsigned h){fnv_rgba_box_half(image,w,h);}
void encode_block(const unsigned char *src,unsigned char *dst){fnv_stb_compress_dxt_block(dst,src,1,STB_DXT_NORMAL);}
uint32_t complete(const unsigned char *src,uint32_t size,unsigned w,unsigned h,unsigned levels,unsigned maximum,uint32_t budget,unsigned char **out,uint32_t *bytes){return fnv_bc_complete_dds(src,size,w,h,levels,maximum,0x35545844,budget,out,bytes);}
void free_chain(void *p){free(p);}
static HWND window;static IDirect3D9 *api;static IDirect3DDevice9 *device;static IDirect3DSurface9 *surface;static HMODULE module;
typedef HRESULT(WINAPI *load_fn)(IDirect3DSurface9 *,const PALETTEENTRY *,const RECT *,const void *,D3DFORMAT,UINT,const PALETTEENTRY *,const RECT *,DWORD,D3DCOLOR);
static load_fn load;
int native_init(void){
    window=CreateWindowExA(0,"STATIC","BC3 oracle",WS_POPUP,0,0,4,4,NULL,NULL,GetModuleHandleA(NULL),NULL);if(!window)return 0;
    api=Direct3DCreate9(32);if(!api)return 0;D3DPRESENT_PARAMETERS p={0};p.Windowed=1;p.SwapEffect=1;p.hDeviceWindow=window;
    if(FAILED(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device)))return 0;
    if(FAILED(IDirect3DDevice9_CreateOffscreenPlainSurface(device,4,4,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&surface,NULL)))return 0;
    module=LoadLibraryExA("d3dx9_38.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return 0;
    FARPROC symbol=GetProcAddress(module,"D3DXLoadSurfaceFromMemory");memcpy(&load,&symbol,sizeof(load));return load!=NULL;
}
int native_decode_format(const void *block,uint32_t format,unsigned char *rgba){
    RECT rect={0,0,4,4};if(FAILED(load(surface,NULL,NULL,block,(D3DFORMAT)format,format==0x31545844?8:16,NULL,&rect,1,0)))return 0;
    D3DLOCKED_RECT mapped;if(FAILED(IDirect3DSurface9_LockRect(surface,&mapped,NULL,D3DLOCK_READONLY)))return 0;
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){const unsigned char *p=(const unsigned char *)mapped.pBits+y*mapped.Pitch+x*4;
        unsigned char *out=rgba+(y*4+x)*4;out[0]=p[2];out[1]=p[1];out[2]=p[0];out[3]=p[3];}
    return SUCCEEDED(IDirect3DSurface9_UnlockRect(surface));
}
int native_decode(const void *block,unsigned char *rgba){return native_decode_format(block,0x35545844,rgba);}
void native_close(void){IDirect3DSurface9_Release(surface);IDirect3DDevice9_Release(device);IDirect3D9_Release(api);DestroyWindow(window);FreeLibrary(module);}
