#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <string.h>
#include <stdint.h>
static HWND window;
static HMODULE compiler;
static pD3DDisassemble disassemble;
static IDirect3D9 *api;
static IDirect3DDevice9 *device;
void shader_validator_close(void){
    if(device){IDirect3DDevice9_Release(device);device=NULL;}
    if(api){IDirect3D9_Release(api);api=NULL;}
    if(window){DestroyWindow(window);window=NULL;}
    if(compiler){FreeLibrary(compiler);compiler=NULL;disassemble=NULL;}
}
uint32_t shader_validator_open(void){
    if(device)return 0x8876086cu;
    compiler=LoadLibraryExA("d3dcompiler_47.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!compiler)return 0x80004005u;
    FARPROC proc=GetProcAddress(compiler,"D3DDisassemble");memcpy(&disassemble,&proc,sizeof(proc));if(!disassemble){shader_validator_close();return 0x80004005u;}
    window=CreateWindowExA(0,"STATIC","Shader validation",WS_POPUP,0,0,4,4,NULL,NULL,GetModuleHandleA(NULL),NULL);
    if(!window){shader_validator_close();return 0x80004005u;}
    api=Direct3DCreate9(D3D_SDK_VERSION);if(!api){shader_validator_close();return 0x80004005u;}
    D3DPRESENT_PARAMETERS p={0};p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    HRESULT hr=IDirect3D9_CreateDevice(api,0,D3DDEVTYPE_NULLREF,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&device);
    if(FAILED(hr))shader_validator_close();return (uint32_t)hr;
}
/* Bounded native disassembly first, then native shader creation. No drawing. */
uint32_t shader_validator_check(const uint32_t *code,uint32_t bytes){
    if(!device || !code || bytes<8 || bytes>1048576 || bytes%4 || code[bytes/4-1]!=0xffff)return 0x8876086cu;
    ID3DBlob *blob=NULL;HRESULT hr=disassemble(code,bytes,0,NULL,&blob);if(blob)ID3D10Blob_Release(blob);if(FAILED(hr))return (uint32_t)hr;
    if(code[0]>>16==0xfffe){IDirect3DVertexShader9 *shader=NULL;hr=IDirect3DDevice9_CreateVertexShader(device,(const DWORD*)code,&shader);if(shader)IDirect3DVertexShader9_Release(shader);}
    else if(code[0]>>16==0xffff){IDirect3DPixelShader9 *shader=NULL;hr=IDirect3DDevice9_CreatePixelShader(device,(const DWORD*)code,&shader);if(shader)IDirect3DPixelShader9_Release(shader);}
    else return 0x8876086cu;
    return (uint32_t)hr;
}
