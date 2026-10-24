#ifndef FNV_CATALOG_PAIR_ORACLE_H
#define FNV_CATALOG_PAIR_ORACLE_H
#ifndef FNV_CATALOG_SAMPLER_MASK
#define FNV_CATALOG_SAMPLER_MASK 1
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
static void secondary_texels(const unsigned char *source,unsigned char *out){
    for(unsigned i=0;i<4;i++){out[i*4]=(unsigned char)(255-source[i*4+2]);out[i*4+1]=source[((i+1)%4)*4];out[i*4+2]=source[i*4+1];out[i*4+3]=(unsigned char)(source[i*4+3]^127);}
}
#endif
#ifdef FNV_CATALOG_MIPS
#include "catalog_mip_fixture.h"
#endif
static unsigned char *load(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*bytes=(uint32_t)n;return p;
}
static const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
static void oracle(unsigned frame,const unsigned char *vs,const unsigned char *ps,const float *vertices,const unsigned char *texels,const float *vc,const float *pc,unsigned char *pixels){
    int null_texture=(frame&16u)!=0;frame&=15u;
    HWND w=CreateWindowExA(0,"STATIC","Catalog shader oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);D3DPRESENT_PARAMETERS pp={0};pp.Windowed=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=w;
    IDirect3DDevice9 *d=NULL;assert(!api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d));
    IDirect3DSurface9 *target=NULL,*read=NULL;assert(!d->lpVtbl->CreateRenderTarget(d,32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));
    assert(!d->lpVtbl->SetDepthStencilSurface(d,NULL) && !d->lpVtbl->SetRenderTarget(d,0,target));
    assert(!d->lpVtbl->CreateOffscreenPlainSurface(d,32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,NULL));
    IDirect3DVertexShader9 *v=NULL;IDirect3DPixelShader9 *p=NULL;IDirect3DVertexDeclaration9 *decl=NULL;IDirect3DTexture9 *texture=NULL;
    assert(!d->lpVtbl->CreateVertexShader(d,(const DWORD*)vs,&v) && !d->lpVtbl->CreatePixelShader(d,(const DWORD*)ps,&p));
    assert(!d->lpVtbl->CreateVertexDeclaration(d,elements,&decl));
    assert(!d->lpVtbl->SetVertexShader(d,v) && !d->lpVtbl->SetPixelShader(d,p) && !d->lpVtbl->SetVertexDeclaration(d,decl));
    assert(!d->lpVtbl->SetVertexShaderConstantF(d,0,vc,256) && !d->lpVtbl->SetPixelShaderConstantF(d,0,pc,224));
    assert(!d->lpVtbl->SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE) && !d->lpVtbl->SetRenderState(d,D3DRS_ZENABLE,FALSE));
    D3DLOCKED_RECT lock;
#ifdef FNV_CATALOG_MIPS
    (void)texels;
    assert(!d->lpVtbl->CreateTexture(d,8,8,4,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,NULL));
    for(unsigned level=0;level<4;level++){
        unsigned char data[256];mip_texels(level,data);unsigned width=8u>>level;
        assert(!texture->lpVtbl->LockRect(texture,level,&lock,NULL,0));
        for(unsigned y=0;y<width;y++)memcpy((unsigned char*)lock.pBits+y*lock.Pitch,data+y*width*4,width*4);
        assert(!texture->lpVtbl->UnlockRect(texture,level));
    }
    assert(!d->lpVtbl->SetTexture(d,0,(IDirect3DBaseTexture9*)texture));
    assert(!d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MAXMIPLEVEL,mip_clamp[frame]) && !d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MIPFILTER,mip_filter[frame]));
#else
    assert(!d->lpVtbl->CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,NULL));
    assert(!texture->lpVtbl->LockRect(texture,0,&lock,NULL,0));
    for(unsigned y=0;y<2;y++)memcpy((unsigned char*)lock.pBits+y*lock.Pitch,texels+y*8,8);
    assert(!texture->lpVtbl->UnlockRect(texture,0) && !d->lpVtbl->SetTexture(d,0,(IDirect3DBaseTexture9*)texture));
#endif
    assert(!d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MINFILTER,D3DTEXF_POINT) && !d->lpVtbl->SetSamplerState(d,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
#ifdef FNV_CATALOG_RENDER_TEXTURE
    IDirect3DTexture9 *render_texture=NULL;IDirect3DSurface9 *render_surface=NULL;
    assert(!d->lpVtbl->CreateTexture(d,32,32,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&render_texture,NULL));
    assert(!render_texture->lpVtbl->GetSurfaceLevel(render_texture,0,&render_surface));
    assert(!d->lpVtbl->SetRenderTarget(d,0,render_surface) && !d->lpVtbl->Clear(d,0,NULL,D3DCLEAR_TARGET,0xff000000,0,0));
    assert(!d->lpVtbl->BeginScene(d) && !d->lpVtbl->DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,vertices,24) && !d->lpVtbl->EndScene(d));
    assert(!d->lpVtbl->SetRenderTarget(d,0,target) && !d->lpVtbl->SetTexture(d,0,(IDirect3DBaseTexture9*)render_texture));
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
    IDirect3DTexture9 *secondary=NULL;unsigned char second[16];secondary_texels(texels,second);
    assert(!d->lpVtbl->CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&secondary,NULL));
    assert(!secondary->lpVtbl->LockRect(secondary,0,&lock,NULL,0));
    for(unsigned y=0;y<2;y++)memcpy((unsigned char*)lock.pBits+y*lock.Pitch,second+y*8,8);
    assert(!secondary->lpVtbl->UnlockRect(secondary,0) && !d->lpVtbl->SetTexture(d,1,(IDirect3DBaseTexture9*)secondary));
    assert(!d->lpVtbl->SetSamplerState(d,1,D3DSAMP_MINFILTER,D3DTEXF_POINT) && !d->lpVtbl->SetSamplerState(d,1,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    assert(!d->lpVtbl->SetSamplerState(d,1,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP) && !d->lpVtbl->SetSamplerState(d,1,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
#endif
    if(null_texture)assert(!d->lpVtbl->SetTexture(d,0,NULL));
    assert(!d->lpVtbl->Clear(d,0,NULL,D3DCLEAR_TARGET,0xff000000,0,0));
    if(frame==5){D3DVIEWPORT9 viewport={4,2,24,28,0,1};assert(!d->lpVtbl->SetViewport(d,&viewport));}
    assert(!d->lpVtbl->BeginScene(d) && !d->lpVtbl->DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,vertices,24) && !d->lpVtbl->EndScene(d));
    assert(!d->lpVtbl->GetRenderTargetData(d,target,read) && !read->lpVtbl->LockRect(read,&lock,NULL,D3DLOCK_READONLY));
    for(unsigned y=0;y<32;y++)memcpy(pixels+y*128,(const unsigned char*)lock.pBits+y*lock.Pitch,128);
    assert(!read->lpVtbl->UnlockRect(read));read->lpVtbl->Release(read);target->lpVtbl->Release(target);
    texture->lpVtbl->Release(texture);decl->lpVtbl->Release(decl);v->lpVtbl->Release(v);p->lpVtbl->Release(p);
#ifdef FNV_CATALOG_RENDER_TEXTURE
    render_surface->lpVtbl->Release(render_surface);render_texture->lpVtbl->Release(render_texture);
#endif
#ifdef FNV_CATALOG_TWO_TEXTURES
    secondary->lpVtbl->Release(secondary);
#endif
    d->lpVtbl->Release(d);api->lpVtbl->Release(api);DestroyWindow(w);
}
#endif
