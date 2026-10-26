/* Own 16x16 Windows D3D9 oracle. No audio, assets, or visible window. */
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#define OK(x) do{HRESULT r=(x);if(FAILED(r)){fprintf(stderr,"line %d hr=%08lx\n",__LINE__,(unsigned long)r);return 1;}}while(0)
static DWORD *load(const char *path){FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<8 || n>1048576 || n%4){fclose(f);return NULL;}DWORD *p=malloc((size_t)n);if(!p || fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}fclose(f);return p;}
int main(int argc,char **argv){
    if(argc!=4)return 1;
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FnvSamplerTypeOracle";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,32,32,NULL,NULL,wc.hInstance,NULL);if(!window)return 1;
    IDirect3D9 *api=Direct3DCreate9(D3D_SDK_VERSION);if(!api)return 1;
    D3DPRESENT_PARAMETERS pp={0};pp.BackBufferWidth=pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;
    IDirect3DDevice9 *d;OK(IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d));
    IDirect3DVertexShader9 *vs;IDirect3DPixelShader9 *ps[2];DWORD *code=load(argv[1]);if(!code)return 1;OK(IDirect3DDevice9_CreateVertexShader(d,code,&vs));free(code);
    for(unsigned i=0;i<2;i++){code=load(argv[i+2]);if(!code)return 1;OK(IDirect3DDevice9_CreatePixelShader(d,code,ps+i));free(code);}
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};IDirect3DVertexDeclaration9 *decl;
    OK(IDirect3DDevice9_CreateVertexDeclaration(d,elements,&decl));OK(IDirect3DDevice9_SetVertexDeclaration(d,decl));OK(IDirect3DDevice9_SetVertexShader(d,vs));
    IDirect3DTexture9 *flat;IDirect3DCubeTexture9 *cube;OK(IDirect3DDevice9_CreateTexture(d,1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&flat,NULL));
    OK(IDirect3DDevice9_CreateCubeTexture(d,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,NULL));D3DLOCKED_RECT lock;
    OK(IDirect3DTexture9_LockRect(flat,0,&lock,NULL,0));*(DWORD*)lock.pBits=0xffff0000u;OK(IDirect3DTexture9_UnlockRect(flat,0));
    for(unsigned i=0;i<6;i++){OK(IDirect3DCubeTexture9_LockRect(cube,(D3DCUBEMAP_FACES)i,0,&lock,NULL,0));*(DWORD*)lock.pBits=0xff00ff00u;OK(IDirect3DCubeTexture9_UnlockRect(cube,(D3DCUBEMAP_FACES)i,0));}
    IDirect3DSurface9 *target,*readback;OK(IDirect3DDevice9_CreateRenderTarget(d,16,16,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,NULL));
    OK(IDirect3DDevice9_CreateOffscreenPlainSurface(d,16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,NULL));OK(IDirect3DDevice9_SetRenderTarget(d,0,target));
    OK(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE));OK(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE));
    OK(IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_MINFILTER,D3DTEXF_POINT));OK(IDirect3DDevice9_SetSamplerState(d,0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    const float direction[]={1,0,0,0},vertices[]={-1,-1,-1,3,3,-1};OK(IDirect3DDevice9_SetPixelShaderConstantF(d,0,direction,1));
    puts("{\"status\":\"OBSERVED\",\"cases\":[");
    for(unsigned shader=0;shader<2;shader++)for(unsigned resource=0;resource<3;resource++){
        OK(IDirect3DDevice9_SetPixelShader(d,ps[shader]));IDirect3DBaseTexture9 *texture=resource==0?(IDirect3DBaseTexture9*)cube:resource==1?(IDirect3DBaseTexture9*)flat:NULL;
        HRESULT bind=IDirect3DDevice9_SetTexture(d,0,texture);OK(IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xff0000ffu,1,0));OK(IDirect3DDevice9_BeginScene(d));
        HRESULT draw=IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,1,vertices,8);OK(IDirect3DDevice9_EndScene(d));
        OK(IDirect3DDevice9_GetRenderTargetData(d,target,readback));OK(IDirect3DSurface9_LockRect(readback,&lock,NULL,D3DLOCK_READONLY));DWORD color=*(DWORD*)((char*)lock.pBits+8*lock.Pitch+8*4);OK(IDirect3DSurface9_UnlockRect(readback));
        printf("%s{\"shader\":\"%s\",\"texture\":\"%s\",\"bind\":\"%08lx\",\"draw\":\"%08lx\",\"pixel_argb\":\"%08lx\"}",shader || resource?",\n":"",shader?"2d":"cube",resource==0?"cube":resource==1?"2d":"null",(unsigned long)bind,(unsigned long)draw,(unsigned long)color);
    }
    puts("]}");IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(target);IDirect3DTexture9_Release(flat);IDirect3DCubeTexture9_Release(cube);IDirect3DVertexDeclaration9_Release(decl);IDirect3DVertexShader9_Release(vs);
    for(unsigned i=0;i<2;i++)IDirect3DPixelShader9_Release(ps[i]);IDirect3DDevice9_Release(d);IDirect3D9_Release(api);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return 0;
}
