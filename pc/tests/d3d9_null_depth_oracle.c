/* Original hidden-window Windows D3D9 oracle; no game code, assets or audio. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <assert.h>
#include <stdio.h>
#define OK(x) do{HRESULT hr=(x);if(FAILED(hr)){fprintf(stderr,"line %d hr=%08lx\n",__LINE__,(unsigned long)hr);return 1;}}while(0)
struct vertex {float x,y,z,rhw;DWORD color;};
static HRESULT pixel(IDirect3DDevice9 *d,DWORD *out){
    IDirect3DSurface9 *rt=NULL,*copy=NULL;D3DLOCKED_RECT lock;
    HRESULT hr=IDirect3DDevice9_GetRenderTarget(d,0,&rt);
    if(SUCCEEDED(hr))hr=IDirect3DDevice9_CreateOffscreenPlainSurface(d,16,16,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,NULL);
    if(SUCCEEDED(hr))hr=IDirect3DDevice9_GetRenderTargetData(d,rt,copy);
    if(SUCCEEDED(hr))hr=IDirect3DSurface9_LockRect(copy,&lock,NULL,D3DLOCK_READONLY);
    if(SUCCEEDED(hr)){*out=*(DWORD*)((unsigned char*)lock.pBits+8*lock.Pitch+8*4);hr=IDirect3DSurface9_UnlockRect(copy);}
    if(copy)IDirect3DSurface9_Release(copy);if(rt)IDirect3DSurface9_Release(rt);return hr;
}
int main(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FnvNullDepthOracle";
    assert(RegisterClassW(&wc));HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(window);
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp={0};pp.BackBufferWidth=pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;
    IDirect3DDevice9 *d=NULL;OK(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d));
    IDirect3DSurface9 *depth=NULL;OK(IDirect3DDevice9_CreateDepthStencilSurface(d,16,16,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,NULL));
    OK(IDirect3DDevice9_SetDepthStencilSurface(d,depth));OK(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,TRUE));OK(IDirect3DDevice9_SetRenderState(d,D3DRS_ZWRITEENABLE,TRUE));
    OK(IDirect3DDevice9_SetRenderState(d,D3DRS_ZFUNC,D3DCMP_LESSEQUAL));OK(IDirect3DDevice9_SetRenderState(d,D3DRS_LIGHTING,FALSE));OK(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE));
    OK(IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    OK(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));OK(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    OK(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,0,0));
    struct vertex v[4]={{0,0,.5f,1,0xffff0000},{16,0,.5f,1,0xffff0000},{0,16,.5f,1,0xffff0000},{16,16,.5f,1,0xffff0000}};
    DWORD values[2],state;
    for(unsigned i=0;i<2;++i){
        OK(IDirect3DDevice9_SetDepthStencilSurface(d,i?depth:NULL));OK(IDirect3DDevice9_GetRenderState(d,D3DRS_ZENABLE,&state));assert(state==TRUE);
        OK(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff00ff00,1,0));OK(IDirect3DDevice9_BeginScene(d));
        OK(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLESTRIP,2,v,sizeof(*v)));OK(IDirect3DDevice9_EndScene(d));OK(pixel(d,values+i));
    }
    assert((values[0]&0xffffff)==0xff0000 && (values[1]&0xffffff)==0x00ff00);
    IDirect3DSurface9_Release(depth);IDirect3DDevice9_Release(d);IDirect3D9_Release(api);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    printf("{\"status\":\"PASS\",\"null_depth_rgb\":\"%06lx\",\"reattached_depth_rgb\":\"%06lx\",\"zenable_preserved\":true}\n",(unsigned long)(values[0]&0xffffff),(unsigned long)(values[1]&0xffffff));return 0;
}
