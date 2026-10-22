#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
int main(void){
 HWND w=CreateWindowExA(0,"STATIC","VB FVF oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);IDirect3D9 *api=Direct3DCreate9(32);assert(api);
 D3DPRESENT_PARAMETERS p={0};p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;IDirect3DDevice9 *d=NULL;assert(!api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));
 const DWORD formats[6]={0,0x102,0x112,0x152,0x104,0x4002};assert(!d->lpVtbl->SetFVF(d,0x102));
 for(unsigned i=0;i<6;i++){
  IDirect3DVertexBuffer9 *vb=NULL;assert(!d->lpVtbl->CreateVertexBuffer(d,257,0,formats[i],D3DPOOL_MANAGED,&vb,NULL));D3DVERTEXBUFFER_DESC desc;assert(!vb->lpVtbl->GetDesc(vb,&desc));
  DWORD current=0;assert(desc.FVF==formats[i] && desc.Size==257 && !d->lpVtbl->GetFVF(d,&current) && current==0x102);
  void *mapped=NULL;assert(!vb->lpVtbl->Lock(vb,0,257,&mapped,0) && mapped);memset(mapped,0x5a,257);assert(!vb->lpVtbl->Unlock(vb));vb->lpVtbl->Release(vb);
 }
 d->lpVtbl->Release(d);api->lpVtbl->Release(api);assert(DestroyWindow(w));puts("PASS6 VB FVF metadata, non-multiple size, locks, no implicit SetFVF");return 0;
}