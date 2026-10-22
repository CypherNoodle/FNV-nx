/* Windows HAL oracle for material storage; no shader/lighting pixel claims. */
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
_Static_assert(sizeof(D3DMATERIAL9)==68,"D3DMATERIAL9 ABI");
int main(void){
    HWND w=CreateWindowExA(0,"STATIC","Material oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);D3DPRESENT_PARAMETERS p={0};p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;
    IDirect3DDevice9 *d=NULL;assert(SUCCEEDED(api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d)));
    D3DMATERIAL9 zero={0},m,observed;assert(!d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&zero,&observed,68));
    unsigned checks=1;
    for(unsigned i=0;i<1000;i++){
        float values[17];for(unsigned j=0;j<17;j++)values[j]=(float)((int)((i*7+j*13)%103)-51)*.25f;
        values[16]=(i&1)?-1.0f:10000.0f;memcpy(&m,values,68);
        assert(!d->lpVtbl->SetMaterial(d,&m) && !d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&m,&observed,68));checks++;
    }
    D3DMATERIAL9 live=m,recorded=m;live.Diffuse.r=.25f;recorded.Diffuse.r=.75f;
    assert(!d->lpVtbl->SetMaterial(d,&live) && !d->lpVtbl->BeginStateBlock(d));assert(!d->lpVtbl->SetMaterial(d,&recorded));
    assert(!d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&observed,&live,68));checks++;
    IDirect3DStateBlock9 *b=NULL;assert(!d->lpVtbl->EndStateBlock(d,&b) && !b->lpVtbl->Apply(b));
    assert(!d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&observed,&recorded,68));checks++;
    assert(!d->lpVtbl->SetMaterial(d,&live) && !b->lpVtbl->Capture(b) && !d->lpVtbl->SetMaterial(d,&zero) && !b->lpVtbl->Apply(b));
    assert(!d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&observed,&live,68));checks++;
    b->lpVtbl->Release(b);b=NULL;
    memset(&p,0,sizeof(p));p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;
    HRESULT reset=d->lpVtbl->Reset(d,&p);fprintf(stderr,"Reset=%08lx\n",(unsigned long)reset);assert(!reset);assert(!d->lpVtbl->GetMaterial(d,&observed) && !memcmp(&observed,&zero,68));checks++;
    d->lpVtbl->Release(d);api->lpVtbl->Release(api);assert(DestroyWindow(w));
    printf("{\"status\":\"PASS\",\"material_comparisons\":%u,\"windows_hal\":true,\"gpu_lighting_tested\":false}\n",checks);return 0;
}
