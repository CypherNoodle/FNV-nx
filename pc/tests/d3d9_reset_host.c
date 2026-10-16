/* Independent Windows HAL reference for the supported healthy-device subset. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
int main(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVResetOracle";
    assert(RegisterClassW(&wc));HWND w=CreateWindowExW(0,wc.lpszClassName,L"FNV Reset reference",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);IDirect3DDevice9 *d=NULL;D3DPRESENT_PARAMETERS p={0};
    p.BackBufferWidth=p.BackBufferHeight=32;p.BackBufferCount=1;p.Windowed=TRUE;p.hDeviceWindow=w;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    ok(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));D3DPRESENT_PARAMETERS original=p;
    const float values[4]={1,2,3,4};float observed[4];ok(IDirect3DDevice9_SetVertexShaderConstantF(d,7,values,1));ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE));
    IDirect3DVertexBuffer9 *vb=NULL;ok(IDirect3DDevice9_CreateVertexBuffer(d,16,0,0,D3DPOOL_MANAGED,&vb,NULL));
    unsigned char expected[16];for(unsigned i=0;i<16;++i)expected[i]=(unsigned char)(i*11);void *data=NULL;
    ok(IDirect3DVertexBuffer9_Lock(vb,0,16,&data,0));memcpy(data,expected,16);ok(IDirect3DVertexBuffer9_Unlock(vb));ok(IDirect3DDevice9_SetStreamSource(d,0,vb,0,16));
    p=original;ok(IDirect3DDevice9_Reset(d,&p));ok(IDirect3DDevice9_TestCooperativeLevel(d));
    ok(IDirect3DDevice9_GetVertexShaderConstantF(d,7,observed,1));for(unsigned i=0;i<4;++i)assert(observed[i]==0);
    DWORD cull=0;ok(IDirect3DDevice9_GetRenderState(d,D3DRS_CULLMODE,&cull));assert(cull==D3DCULL_CCW);
    IDirect3DVertexBuffer9 *bound=NULL;UINT offset=99,stride=99;ok(IDirect3DDevice9_GetStreamSource(d,0,&bound,&offset,&stride));assert(!bound && !offset && !stride);
    ok(IDirect3DVertexBuffer9_Lock(vb,0,16,&data,D3DLOCK_READONLY));assert(!memcmp(data,expected,16));ok(IDirect3DVertexBuffer9_Unlock(vb));IDirect3DVertexBuffer9_Release(vb);
    IDirect3DSurface9 *back=NULL;ok(IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,&back));p=original;
    assert(IDirect3DDevice9_Reset(d,&p)==D3DERR_INVALIDCALL);assert(IDirect3DDevice9_TestCooperativeLevel(d)==D3DERR_DEVICENOTRESET);
    IDirect3DSurface9_Release(back);p=original;ok(IDirect3DDevice9_Reset(d,&p));ok(IDirect3DDevice9_TestCooperativeLevel(d));
    p=original;p.BackBufferCount=50;assert(IDirect3DDevice9_Reset(d,&p)==D3DERR_INVALIDCALL);assert(IDirect3DDevice9_TestCooperativeLevel(d)==D3DERR_DEVICENOTRESET);
    p=original;ok(IDirect3DDevice9_Reset(d,&p));ok(IDirect3DDevice9_TestCooperativeLevel(d));
    printf("{\"status\":\"PASS\",\"windows_hal_reference\":true,\"managed_bytes_checked\":16,\"state_defaults_checked\":true,\"failed_reset_retries\":2,\"output_parameters\":[%u,%u,%u,%u]}\n",p.BackBufferWidth,p.BackBufferHeight,p.BackBufferCount,(unsigned)p.BackBufferFormat);
    assert(!IDirect3DDevice9_Release(d));assert(!IDirect3D9_Release(api));DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
