#include <windows.h>
#include <d3d9.h>
#include "d3d9_transform_state.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
int main(void){
    HWND w=CreateWindowExA(0,"STATIC","Transform oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS p={0};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;
    IDirect3DDevice9 *d=NULL;assert(SUCCEEDED(api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d)));
    struct fnv_transform_state bank;fnv_transform_init(&bank);unsigned checks=0;
    for(uint32_t state=0;state<512;state++){
        if(fnv_transform_index(state)<0)continue;
        D3DMATRIX actual,expected,input;float *values=(float *)&input;
        assert(!fnv_transform_get(&bank,state,(float *)&actual));
        assert(SUCCEEDED(d->lpVtbl->GetTransform(d,(D3DTRANSFORMSTATETYPE)state,&expected)));
        assert(!memcmp(&actual,&expected,64));checks++;
        /* Small integers make sums exact; this tests order, not a claim about
         * host-specific rounding of arbitrary floating-point products. */
        for(unsigned j=0;j<16;j++)values[j]=(float)((int)((state+j*3)%7)-3);
        assert(!fnv_transform_set(&bank,state,values));assert(SUCCEEDED(d->lpVtbl->SetTransform(d,(D3DTRANSFORMSTATETYPE)state,&input)));
        for(unsigned j=0;j<16;j++)values[j]=(float)((int)((state*3+j)%5)-2);
        assert(!fnv_transform_multiply(&bank,state,values));assert(SUCCEEDED(d->lpVtbl->MultiplyTransform(d,(D3DTRANSFORMSTATETYPE)state,&input)));
        assert(!fnv_transform_get(&bank,state,(float *)&actual));assert(SUCCEEDED(d->lpVtbl->GetTransform(d,(D3DTRANSFORMSTATETYPE)state,&expected)));
        assert(!memcmp(&actual,&expected,64));checks++;
    }
    struct fnv_transform_state before=bank;float canary[16],input[16]={0};memset(canary,0xa5,64);
    for(unsigned state=0;state<512;state++)if(fnv_transform_index(state)<0){
        assert(fnv_transform_get(&bank,state,canary)==(uint32_t)D3DERR_INVALIDCALL);
        for(unsigned j=0;j<64;j++)assert(((unsigned char *)canary)[j]==0xa5);
        assert(fnv_transform_set(&bank,state,input)==(uint32_t)D3DERR_INVALIDCALL);
        assert(fnv_transform_multiply(&bank,state,input)==(uint32_t)D3DERR_INVALIDCALL);
    }
    assert(!memcmp(&bank,&before,sizeof(bank)));
    assert(fnv_transform_get(NULL,2,input)==(uint32_t)D3DERR_INVALIDCALL && fnv_transform_set(&bank,2,NULL)==(uint32_t)D3DERR_INVALIDCALL);
    /* Source aliases destination: squaring must not overwrite unread input. */
    float saved[16],squared[16];memcpy(saved,bank.matrices[0],64);
    assert(!fnv_transform_multiply(&bank,2,saved));memcpy(squared,bank.matrices[0],64);
    assert(!fnv_transform_set(&bank,2,saved));assert(!fnv_transform_multiply(&bank,2,bank.matrices[0]));assert(!memcmp(squared,bank.matrices[0],64));
    d->lpVtbl->Release(d);api->lpVtbl->Release(api);assert(DestroyWindow(w));
    printf("PASS transform comparisons=%u; invalid states unchanged; alias checked; GPU path pending\n",checks);return 0;
}
