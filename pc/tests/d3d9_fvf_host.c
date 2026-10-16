#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "d3d9_fvf.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
int main(void){
    _Static_assert(sizeof(D3DVERTEXELEMENT9)==8,"packed declaration");
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVFVFReference";assert(RegisterClassW(&wc));
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"FNV FVF reference",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);IDirect3DDevice9 *d=NULL;D3DPRESENT_PARAMETERS p={0};p.BackBufferWidth=p.BackBufferHeight=32;p.Windowed=TRUE;p.hDeviceWindow=w;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
    ok(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));unsigned checks=0;
    const unsigned positions[3]={2,4,0x4002};
    for(unsigned position=0;position<3;++position)for(unsigned flags=0;flags<16;++flags)for(unsigned tex=0;tex<=8;++tex){
        unsigned fvf=positions[position]|(flags<<4)|(tex<<8);if(position==1 && (flags&1))continue;
        for(unsigned i=0;i<tex;++i)fvf|=((i+flags)%4)<<(16+i*2);
        struct fnv_fvf_layout expected;assert(!fnv_d3d9_fvf_layout(fvf,&expected));ok(IDirect3DDevice9_SetFVF(d,fvf));DWORD observed=0;ok(IDirect3DDevice9_GetFVF(d,&observed));assert(observed==fvf);
        IDirect3DVertexDeclaration9 *decl=NULL;ok(IDirect3DDevice9_GetVertexDeclaration(d,&decl));assert(decl);D3DVERTEXELEMENT9 elements[65];UINT count=65;ok(IDirect3DVertexDeclaration9_GetDeclaration(decl,elements,&count));
        if(count!=expected.count || memcmp(elements,expected.elements,count*8)){fprintf(stderr,"FVF mismatch %08x count=%u expected=%u\n",fvf,count,expected.count);return 1;}
        ok(IDirect3DDevice9_SetVertexDeclaration(d,decl));ok(IDirect3DDevice9_GetFVF(d,&observed));assert(observed==fvf);IDirect3DVertexDeclaration9_Release(decl);checks++;
    }
    ok(IDirect3DDevice9_SetFVF(d,0));IDirect3DVertexDeclaration9 *empty=NULL;ok(IDirect3DDevice9_GetVertexDeclaration(d,&empty));assert(empty);UINT count=0;ok(IDirect3DVertexDeclaration9_GetDeclaration(empty,NULL,&count));assert(count==1);IDirect3DVertexDeclaration9_Release(empty);
    const unsigned invalid[]={1,6,8,10,12,14,0x1002,0x2002,0x8002,0x900,0x14,0x10002};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i){struct fnv_fvf_layout result,saved;memset(&result,0xa5,sizeof(result));saved=result;assert(fnv_d3d9_fvf_layout(invalid[i],&result)==0x8876086cu && !memcmp(&result,&saved,sizeof(result)));}
    assert(!IDirect3DDevice9_Release(d));assert(!IDirect3D9_Release(api));DestroyWindow(w);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    printf("{\"status\":\"PASS\",\"windows_hal_reference\":true,\"fvf_layout_cases\":%u,\"negative_cases\":12}\n",checks);return 0;
}
