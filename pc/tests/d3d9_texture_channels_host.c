/* Independent actual D3D9 HAL sampling versus our CPU upload conversion. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "d3d9_texture_upload.h"
static void checked(HRESULT hr,const char *step){if(FAILED(hr)){fprintf(stderr,"%s hr=%08lx\n",step,(unsigned long)hr);exit(2);}}
int main(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVTextureChannels";assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"FNV texture channels",WS_POPUP,0,0,16,16,NULL,NULL,wc.hInstance,NULL);assert(window);
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);IDirect3DDevice9 *device=NULL;
    D3DPRESENT_PARAMETERS present={0};present.BackBufferWidth=present.BackBufferHeight=16;present.BackBufferCount=1;present.Windowed=TRUE;present.hDeviceWindow=window;present.SwapEffect=D3DSWAPEFFECT_DISCARD;
    checked(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&present,&device),"Create HAL");
    D3DADAPTER_IDENTIFIER9 adapter;checked(IDirect3D9_GetAdapterIdentifier(api,0,0,&adapter),"AdapterIdentifier");printf("HAL: %s\n",adapter.Description);
    HMODULE compiler=LoadLibraryExW(L"d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(compiler);FARPROC symbol=GetProcAddress(compiler,"D3DCompile");pD3DCompile compile;memcpy(&compile,&symbol,sizeof(compile));assert(compile);
    const char code[]="sampler2D image:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv);}";
    ID3DBlob *blob=NULL,*errors=NULL;HRESULT hr=compile(code,strlen(code),"own-channel-probe",NULL,NULL,"main","ps_2_0",0,0,&blob,&errors);
    if(errors){fprintf(stderr,"%s",(const char*)ID3D10Blob_GetBufferPointer(errors));ID3D10Blob_Release(errors);}checked(hr,"Compile PS");
    IDirect3DPixelShader9 *shader=NULL;checked(IDirect3DDevice9_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(blob),&shader),"Create PS");ID3D10Blob_Release(blob);
    IDirect3DSurface9 *target=NULL,*readback=NULL;checked(IDirect3DDevice9_CreateRenderTarget(device,16,16,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL),"Create RT");
    checked(IDirect3DDevice9_CreateOffscreenPlainSurface(device,16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,NULL),"Create readback");checked(IDirect3DDevice9_SetDepthStencilSurface(device,NULL),"No depth");checked(IDirect3DDevice9_SetRenderTarget(device,0,target),"Set RT");
    D3DVIEWPORT9 view={0,0,16,16,0,1};checked(IDirect3DDevice9_SetViewport(device,&view),"Viewport");
    const D3DRENDERSTATETYPE rs[]={D3DRS_LIGHTING,D3DRS_ZENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE};
    for(unsigned i=0;i<sizeof(rs)/sizeof(rs[0]);++i)checked(IDirect3DDevice9_SetRenderState(device,rs[i],0),"Render state");
    checked(IDirect3DDevice9_SetRenderState(device,D3DRS_CULLMODE,D3DCULL_NONE),"Cull");checked(IDirect3DDevice9_SetRenderState(device,D3DRS_COLORWRITEENABLE,15),"Write RGBA");
    checked(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_MINFILTER,D3DTEXF_POINT),"Min filter");checked(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT),"Mag filter");
    checked(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_MIPFILTER,D3DTEXF_NONE),"Mip filter");checked(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP),"Address U");checked(IDirect3DDevice9_SetSamplerState(device,0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP),"Address V");
    checked(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"Color op");checked(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_COLORARG1,D3DTA_TEXTURE),"Color arg");
    checked(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),"Alpha op");checked(IDirect3DDevice9_SetTextureStageState(device,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE),"Alpha arg");
    checked(IDirect3DDevice9_SetTextureStageState(device,1,D3DTSS_COLOROP,D3DTOP_DISABLE),"Disable stage1");
    checked(IDirect3DDevice9_SetFVF(device,D3DFVF_XYZRHW|D3DFVF_TEX1),"FVF");
    struct vertex {float x,y,z,rhw,u,v;} vertices[4]={{-.5f,-.5f,0,1,0,0},{15.5f,-.5f,0,1,1,0},{-.5f,15.5f,0,1,0,1},{15.5f,15.5f,0,1,1,1}};
    const D3DFORMAT formats[]={D3DFMT_A8,D3DFMT_A8R8G8B8,D3DFMT_L8,D3DFMT_R32F};unsigned pixels_checked=0;
    for(unsigned format=0;format<4;++format){IDirect3DTexture9 *texture=NULL;unsigned unit=(format==1 || format==3)?4:1;unsigned char input[1024],rgba[1024];
        for(unsigned i=0;i<256;++i){if(format==3){float value=i/255.0f;memcpy(input+i*4,&value,4);rgba[i*4]=(unsigned char)i;rgba[i*4+1]=rgba[i*4+2]=255;rgba[i*4+3]=255;}
            else if(unit==4){input[i*4]=113;input[i*4+1]=57;input[i*4+2]=17;input[i*4+3]=(unsigned char)i;}else input[i]=(unsigned char)i;}
        if(format!=3){struct fnv_texture_patch patch={16,16,0,0,16,16,(uint32_t)formats[format],16*unit,input,256*unit};assert(!fnv_d3d9_texture_rgba8(&patch,rgba,sizeof(rgba),64));}
        checked(IDirect3DDevice9_CreateTexture(device,16,16,1,0,formats[format],D3DPOOL_MANAGED,&texture,NULL),"Create texture");D3DLOCKED_RECT mapped;
        checked(IDirect3DTexture9_LockRect(texture,0,&mapped,NULL,0),"Lock texture");for(unsigned y=0;y<16;++y)memcpy((unsigned char*)mapped.pBits+y*mapped.Pitch,input+y*16*unit,16*unit);checked(IDirect3DTexture9_UnlockRect(texture,0),"Unlock texture");
        checked(IDirect3DDevice9_SetTexture(device,0,(IDirect3DBaseTexture9*)texture),"Bind texture");
        for(unsigned programmable=0;programmable<2;++programmable){checked(IDirect3DDevice9_SetPixelShader(device,programmable?shader:NULL),"Bind PS");
            checked(IDirect3DDevice9_Clear(device,0,NULL,D3DCLEAR_TARGET,0x7f123456,1,0),"Clear");checked(IDirect3DDevice9_BeginScene(device),"BeginScene");
            checked(IDirect3DDevice9_DrawPrimitiveUP(device,D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0])),"DrawPrimitiveUP");checked(IDirect3DDevice9_EndScene(device),"EndScene");
            checked(IDirect3DDevice9_GetRenderTargetData(device,target,readback),"GPU readback");checked(IDirect3DSurface9_LockRect(readback,&mapped,NULL,D3DLOCK_READONLY),"Lock readback");
            for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){unsigned char *p=(unsigned char*)mapped.pBits+y*mapped.Pitch+x*4,*e=rgba+(y*16+x)*4;
                if(p[0]!=e[2] || p[1]!=e[1] || p[2]!=e[0] || p[3]!=e[3]){fprintf(stderr,"channel mismatch format=%u programmable=%u xy=%u,%u Windows BGRA=%u,%u,%u,%u converter RGBA=%u,%u,%u,%u\n",(unsigned)formats[format],programmable,x,y,p[0],p[1],p[2],p[3],e[0],e[1],e[2],e[3]);return 3;}pixels_checked++;}
            checked(IDirect3DSurface9_UnlockRect(readback),"Unlock readback");printf("format=%u programmable=%u PASS 256 pixels\n",(unsigned)formats[format],programmable);}
        checked(IDirect3DDevice9_SetTexture(device,0,NULL),"Unbind");IDirect3DTexture9_Release(texture);
    }
    IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(target);IDirect3DPixelShader9_Release(shader);IDirect3DDevice9_Release(device);IDirect3D9_Release(api);FreeLibrary(compiler);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    printf("{\"status\":\"PASS\",\"real_d3d9_hal\":true,\"formats\":4,\"pipelines\":2,\"gpu_pixel_checks\":%u,\"a8_rgb_zero\":true}\n",pixels_checked);return 0;
}
