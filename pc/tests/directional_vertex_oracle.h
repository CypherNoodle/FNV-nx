#ifndef FNV_DIRECTIONAL_VERTEX_ORACLE_H
#define FNV_DIRECTIONAL_VERTEX_ORACLE_H
#ifdef FNV_FIXED_SWITCH
static unsigned oracle_mode;
#endif
#ifdef FNV_DIRECTIONAL_TEXTURED
/* BGRA A8R8G8B8 input with distinct channels and alpha for all four texels. */
static const unsigned char directional_texels[16]={31,83,211,193, 173,47,109,127, 71,229,53,89, 251,139,17,241};
static void directional_uv(unsigned frame,float *vertices){
    for(unsigned i=0;i<6;i++){
        vertices[i*8+6]=frame==4?(vertices[i*8]+1)*.5f:((frame&1)?.75f:.25f);
        vertices[i*8+7]=frame==4?(1-vertices[i*8+1])*.5f:((frame&2)?.75f:.25f);
    }
}
#endif
static void oracle(unsigned frame,const float *vertices,const struct fnv_transform_state *bank,const uint32_t material[17],const uint32_t light[26],uint32_t ambient,unsigned char *pixels){
    HWND w=CreateWindowExA(0,"STATIC","Directional oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);D3DPRESENT_PARAMETERS pp={0};pp.Windowed=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;
    IDirect3DDevice9 *d=NULL;assert(!api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d));
#ifdef FNV_FIXED_AUTOMATIC
    const DWORD defaults[2][6]={{4,2,1,2,2,1},{1,2,1,1,2,1}};
    for(unsigned stage=0;stage<2;stage++)for(unsigned i=0;i<6;i++){
        DWORD observed=0;assert(!d->lpVtbl->GetTextureStageState(d,stage,(D3DTEXTURESTAGESTATETYPE)(i+1),&observed) && observed==defaults[stage][i]);
    }
#endif
    IDirect3DSurface9 *target=NULL,*read=NULL;assert(!d->lpVtbl->CreateRenderTarget(d,32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));
    assert(!d->lpVtbl->SetDepthStencilSurface(d,NULL) && !d->lpVtbl->SetRenderTarget(d,0,target));
    assert(!d->lpVtbl->CreateOffscreenPlainSurface(d,32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,NULL));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE) && !d->lpVtbl->SetRenderState(d,D3DRS_ZENABLE,FALSE));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_COLORVERTEX,FALSE) && !d->lpVtbl->SetRenderState(d,D3DRS_NORMALIZENORMALS,frame!=6));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_AMBIENT,ambient) && !d->lpVtbl->SetMaterial(d,(const D3DMATERIAL9*)material));
    assert(!d->lpVtbl->SetTransform(d,D3DTS_WORLD,(const D3DMATRIX*)bank->matrices[10]) && !d->lpVtbl->SetTransform(d,D3DTS_VIEW,(const D3DMATRIX*)bank->matrices[0]));
    assert(!d->lpVtbl->SetTransform(d,D3DTS_PROJECTION,(const D3DMATRIX*)bank->matrices[1]));
    assert(!d->lpVtbl->SetLight(d,0,(const D3DLIGHT9*)light) && !d->lpVtbl->LightEnable(d,0,frame!=3));
#ifdef FNV_DIRECTIONAL_TEXTURED
    IDirect3DTexture9 *texture=NULL;assert(!d->lpVtbl->CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,NULL));
    D3DLOCKED_RECT texlock;assert(!texture->lpVtbl->LockRect(texture,0,&texlock,NULL,0));
    for(unsigned y=0;y<2;y++)memcpy((unsigned char*)texlock.pBits+y*texlock.Pitch,directional_texels+y*8,8);
    assert(!texture->lpVtbl->UnlockRect(texture,0) && !d->lpVtbl->SetTexture(d,0,(IDirect3DBaseTexture9*)texture));
    assert(!d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MINFILTER,D3DTEXF_POINT) && !d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_MODULATE));
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_TEXTURE) && !d->lpVtbl->SetTextureStageState(d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE));
#ifdef FNV_FIXED_SWITCH
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAOP,oracle_mode==2?D3DTOP_SELECTARG1:D3DTOP_MODULATE));
#elif defined(FNV_FIXED_ALPHA_TEXTURE)
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
#else
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_MODULATE));
#endif
    assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE) && !d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE));
#endif
#ifdef FNV_FIXED_SWITCH
    if(!oracle_mode){
        assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG1) && !d->lpVtbl->SetTextureStageState(d,0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
        assert(!d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1) && !d->lpVtbl->SetTextureStageState(d,0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));
    }
#endif
    assert(!d->lpVtbl->SetFVF(d,0x112));assert(!d->lpVtbl->Clear(d,0,NULL,D3DCLEAR_TARGET,0xff000000,0,0));
    assert(!d->lpVtbl->BeginScene(d) && !d->lpVtbl->DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,vertices,32) && !d->lpVtbl->EndScene(d));
    assert(!d->lpVtbl->GetRenderTargetData(d,target,read));D3DLOCKED_RECT lock;assert(!read->lpVtbl->LockRect(read,&lock,NULL,D3DLOCK_READONLY));
    for(unsigned y=0;y<32;y++)memcpy(pixels+y*128,(const unsigned char*)lock.pBits+y*lock.Pitch,128);
    assert(!read->lpVtbl->UnlockRect(read));read->lpVtbl->Release(read);target->lpVtbl->Release(target);
#ifdef FNV_DIRECTIONAL_TEXTURED
    texture->lpVtbl->Release(texture);
#endif
    d->lpVtbl->Release(d);api->lpVtbl->Release(api);DestroyWindow(w);
}
#endif
