#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static DWORD cull(IDirect3DDevice9 *d){DWORD v=0;ok(IDirect3DDevice9_GetRenderState(d,D3DRS_CULLMODE,&v));return v;}
#include "stateblock_constants_oracle.h"
#include "stateblock_textures_oracle.h"
int main(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVStateBlockReference";assert(RegisterClassW(&wc));
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"FNV stateblock reference",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);IDirect3DDevice9 *d=NULL;D3DPRESENT_PARAMETERS p={0};p.BackBufferWidth=p.BackBufferHeight=32;p.Windowed=TRUE;p.hDeviceWindow=w;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    ok(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));
    ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_CCW));ok(IDirect3DDevice9_BeginStateBlock(d));assert(IDirect3DDevice9_BeginStateBlock(d)==D3DERR_INVALIDCALL);
    ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_CW));ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE));assert(cull(d)==D3DCULL_CCW);
    IDirect3DStateBlock9 *b=NULL;ok(IDirect3DDevice9_EndStateBlock(d,&b));assert(b && cull(d)==D3DCULL_CCW);ok(IDirect3DStateBlock9_Apply(b));assert(cull(d)==D3DCULL_NONE);
    ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_CW));ok(IDirect3DStateBlock9_Capture(b));ok(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_CCW));ok(IDirect3DStateBlock9_Apply(b));assert(cull(d)==D3DCULL_CW);
    ok(IDirect3DDevice9_BeginStateBlock(d));assert(IDirect3DStateBlock9_Capture(b)==D3DERR_INVALIDCALL);IDirect3DStateBlock9 *empty=NULL;ok(IDirect3DDevice9_EndStateBlock(d,&empty));ok(IDirect3DStateBlock9_Apply(empty));ok(IDirect3DStateBlock9_Capture(empty));IDirect3DStateBlock9_Release(empty);
    IDirect3DDevice9 *parent=NULL;ok(IDirect3DStateBlock9_GetDevice(b,&parent));assert(parent==d);IDirect3DDevice9_Release(parent);
    IDirect3DStateBlock9 *queried=NULL;ok(IDirect3DStateBlock9_QueryInterface(b,&IID_IDirect3DStateBlock9,(void**)&queried));assert(queried==b);IDirect3DStateBlock9_Release(queried);
    IDirect3DStateBlock9_Release(b);constant_blocks(d);texture_blocks(d);assert(!IDirect3DDevice9_Release(d));assert(!IDirect3D9_Release(api));DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    printf("{\"status\":\"PASS\",\"windows_hal_reference\":true,\"stateblock_semantics_checked\":true,\"constant_bank_comparisons\":%u,\"constant_blocks\":64,\"snapshot_negatives\":4,\"texture_binding_checks\":%u}\n",snapshot_checks,texture_block_checks);return 0;
}
