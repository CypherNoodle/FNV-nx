#ifndef FNV_DIRECTIONAL_VERTEX_ORACLE_H
#define FNV_DIRECTIONAL_VERTEX_ORACLE_H
static void oracle(unsigned frame,const float *vertices,const struct fnv_transform_state *bank,const uint32_t material[17],const uint32_t light[26],uint32_t ambient,unsigned char *pixels){
    HWND w=CreateWindowExA(0,"STATIC","Directional oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);D3DPRESENT_PARAMETERS pp={0};pp.Windowed=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;
    IDirect3DDevice9 *d=NULL;assert(!api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d));
    IDirect3DSurface9 *target=NULL,*read=NULL;assert(!d->lpVtbl->CreateRenderTarget(d,32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));
    assert(!d->lpVtbl->SetDepthStencilSurface(d,NULL) && !d->lpVtbl->SetRenderTarget(d,0,target));
    assert(!d->lpVtbl->CreateOffscreenPlainSurface(d,32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,NULL));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE) && !d->lpVtbl->SetRenderState(d,D3DRS_ZENABLE,FALSE));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_COLORVERTEX,FALSE) && !d->lpVtbl->SetRenderState(d,D3DRS_NORMALIZENORMALS,frame!=6));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_AMBIENT,ambient) && !d->lpVtbl->SetMaterial(d,(const D3DMATERIAL9*)material));
    assert(!d->lpVtbl->SetTransform(d,D3DTS_WORLD,(const D3DMATRIX*)bank->matrices[10]) && !d->lpVtbl->SetTransform(d,D3DTS_VIEW,(const D3DMATRIX*)bank->matrices[0]));
    assert(!d->lpVtbl->SetTransform(d,D3DTS_PROJECTION,(const D3DMATRIX*)bank->matrices[1]));
    assert(!d->lpVtbl->SetLight(d,0,(const D3DLIGHT9*)light) && !d->lpVtbl->LightEnable(d,0,frame!=3));
    assert(!d->lpVtbl->SetFVF(d,0x112));assert(!d->lpVtbl->Clear(d,0,NULL,D3DCLEAR_TARGET,0xff000000,0,0));
    assert(!d->lpVtbl->BeginScene(d) && !d->lpVtbl->DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,vertices,32) && !d->lpVtbl->EndScene(d));
    assert(!d->lpVtbl->GetRenderTargetData(d,target,read));D3DLOCKED_RECT lock;assert(!read->lpVtbl->LockRect(read,&lock,NULL,D3DLOCK_READONLY));
    for(unsigned y=0;y<32;y++)memcpy(pixels+y*128,(const unsigned char*)lock.pBits+y*lock.Pitch,128);
    assert(!read->lpVtbl->UnlockRect(read));read->lpVtbl->Release(read);target->lpVtbl->Release(target);d->lpVtbl->Release(d);api->lpVtbl->Release(api);DestroyWindow(w);
}
#endif
