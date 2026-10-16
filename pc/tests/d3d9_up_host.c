/* Independent HAL reference for the UP binding side effects. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
struct vertex {float x,y,z,rhw;DWORD color;};
int main(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVUPReference";assert(RegisterClassW(&wc));
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"FNV UP reference",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);IDirect3DDevice9 *d=NULL;D3DPRESENT_PARAMETERS p={0};p.BackBufferWidth=p.BackBufferHeight=32;p.Windowed=TRUE;p.hDeviceWindow=w;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    ok(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));
    IDirect3DVertexBuffer9 *vb=NULL;IDirect3DIndexBuffer9 *ib=NULL;
    ok(IDirect3DDevice9_CreateVertexBuffer(d,128,0,0,D3DPOOL_MANAGED,&vb,NULL));ok(IDirect3DDevice9_CreateIndexBuffer(d,12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,NULL));
    ok(IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    const struct vertex vertices[4]={{0,0,0,1,0},{0,0,0,1,0xffff0000},{32,0,0,1,0xffff0000},{0,32,0,1,0xffff0000}};
    const WORD indices[3]={1,2,3};
    for(unsigned indexed=0;indexed<2;++indexed){
        ok(IDirect3DDevice9_SetStreamSource(d,0,vb,0,sizeof(struct vertex)));ok(IDirect3DDevice9_SetIndices(d,ib));ok(IDirect3DDevice9_BeginScene(d));
        if(indexed)ok(IDirect3DDevice9_DrawIndexedPrimitiveUP(d,D3DPT_TRIANGLELIST,1,3,1,indices,D3DFMT_INDEX16,vertices,sizeof(struct vertex)));
        else ok(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,1,vertices+1,sizeof(struct vertex)));
        ok(IDirect3DDevice9_EndScene(d));IDirect3DVertexBuffer9 *bound=NULL;UINT offset=99,stride=99;
        ok(IDirect3DDevice9_GetStreamSource(d,0,&bound,&offset,&stride));assert(!bound && !offset && !stride);
        IDirect3DIndexBuffer9 *bound_ib=NULL;ok(IDirect3DDevice9_GetIndices(d,&bound_ib));assert(bound_ib==(indexed?NULL:ib));if(bound_ib)IDirect3DIndexBuffer9_Release(bound_ib);
    }
    IDirect3DVertexBuffer9_Release(vb);IDirect3DIndexBuffer9_Release(ib);assert(!IDirect3DDevice9_Release(d));assert(!IDirect3D9_Release(api));DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    puts("{\"status\":\"PASS\",\"windows_hal_reference\":true,\"up_binding_checks\":4}");return 0;
}
