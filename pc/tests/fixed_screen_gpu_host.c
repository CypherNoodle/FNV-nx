#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
#include "d3d9_vk_backend.h"
#include "fixed_vertex_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#undef NDEBUG
#include <assert.h>
struct vertex {float x,y,z,rhw,u,v;};
static unsigned char *load(const char *path,uint32_t *bytes){
    FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0 && n<1048576);rewind(f);
    unsigned char *p=malloc((size_t)n);assert(p && fread(p,1,(size_t)n,f)==(size_t)n && !fclose(f));*bytes=(uint32_t)n;return p;
}
static void set(struct fnv_d3d9_shader_backend *b,void *g,uint32_t name,uint32_t value){uint32_t v[6]={value};assert(!b->state(g,FNV_D3D9_RENDER_STATE,0,name,v,0));}
int main(int argc,char **argv){
    assert(argc==5);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof gp);
    struct fnv_vk_config config={32,32,32,2,32,16u<<20,0};struct fnv_vk_backend *g;VkResult failure;
    assert(!fnv_vk_backend_create(gp,&config,&g,&failure));assert(!fnv_vk_backend_draw_wait(g,10000000000ull));struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(g,&b);
    unsigned char *code[2],*spv[2];uint32_t cb[2],sb[2];uintptr_t shader[2];
    for(unsigned i=0;i<2;i++){
        code[i]=load(argv[1+i],cb+i);spv[i]=load(argv[3+i],sb+i);struct fnv_uniform_plan plan=i?pixel_plan:vertex_plan;
        assert(!fnv_vk_backend_register_shader(g,i,code[i],cb[i],spv[i],sb[i],&plan));assert(!b.create(g,i,code[i],cb[i],shader+i));
    }
    assert(fnv_vk_backend_fixed_screen(g,shader[1],shader[0])==FNV_VK_INVALID);
    assert(fnv_vk_backend_fixed_screen(g,shader[0],0)==FNV_VK_INVALID);
    assert(!fnv_vk_backend_fixed_screen(g,shader[0],shader[1]));
    assert(!fnv_vk_backend_link_program(g,code[0],cb[0],code[1],cb[1],vertex_inputs,2,1));
    const unsigned char elements[24]={0,0,0,0,3,0,9,0,0,0,16,0,1,0,5,0,255,0,0,0,17,0,0,0};uintptr_t decl,tex;
    assert(!b.create_declaration(g,elements,3,&decl));assert(!b.create_texture(g,2,2,1,0,21,1,&tex));
    uint32_t colors[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
    assert(!b.upload_texture(g,tex,0,0,0,2,2,0,8,colors));
    HWND window=CreateWindowExA(0,"STATIC","Silent fixed screen oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(window);
    IDirect3D9 *d3d=Direct3DCreate9(D3D_SDK_VERSION);assert(d3d);D3DPRESENT_PARAMETERS pp={0};
    pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    IDirect3DDevice9 *native;assert(SUCCEEDED(IDirect3D9_CreateDevice(d3d,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&native)));
    IDirect3DTexture9 *nt;assert(SUCCEEDED(IDirect3DDevice9_CreateTexture(native,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&nt,NULL)));
    D3DLOCKED_RECT lock;assert(SUCCEEDED(IDirect3DTexture9_LockRect(nt,0,&lock,NULL,0)));for(unsigned y=0;y<2;y++)memcpy((char*)lock.pBits+y*lock.Pitch,colors+y*2,8);assert(SUCCEEDED(IDirect3DTexture9_UnlockRect(nt,0)));
    IDirect3DSurface9 *target,*readback;assert(SUCCEEDED(IDirect3DDevice9_GetRenderTarget(native,0,&target)));
    assert(SUCCEEDED(IDirect3DDevice9_CreateOffscreenPlainSurface(native,32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,NULL)));
    unsigned checks=0;
    for(unsigned frame=0;frame<3;frame++){
        if(frame==2){b.release(g,shader[0]);b.release(g,shader[1]);shader[0]=shader[1]=0;assert(!fnv_vk_backend_reset(g,10000000000ull));}
        assert(!b.bind_declaration(g,decl) && !b.bind_texture(g,0,tex));set(&b,g,7,0);set(&b,g,137,0);set(&b,g,22,1);
        uint32_t vp[6]={4,6,20,16,0,0x3f800000};assert(!b.state(g,FNV_D3D9_VIEWPORT,0,0,vp,0));
        struct vertex v[4]={{4,6,.5f,1,0,0},{24,6,.5f,1,1,0},{4,22,.5f,1,0,1},{24,22,.5f,1,1,1}};
        if(frame==1){v[1].rhw=.5f;v[2].rhw=1.5f;v[3].rhw=2;}
        uint32_t full_vp[6]={0,0,32,32,0,0x3f800000};assert(!b.state(g,FNV_D3D9_VIEWPORT,0,0,full_vp,0));
        assert(!b.clear(g,0,NULL,1,0xff203040,0,0));assert(!b.state(g,FNV_D3D9_VIEWPORT,0,0,vp,0));assert(!b.scene(g,1));
        uint16_t indices[4]={0,1,2,3};uint32_t hr=frame==1?b.draw_up(g,1,5,0,4,2,indices,101,v,24,sizeof v,sizeof indices):b.draw_up(g,0,5,0,0,2,NULL,0,v,24,sizeof v,0);if(hr)fprintf(stderr,"screen draw %08x\n",hr);assert(!hr);
        assert(!b.scene(g,0));uintptr_t surface;assert(!b.get_target(g,0,0,&surface));unsigned char pixels[4096];assert(!fnv_vk_backend_read_color(g,surface,pixels,sizeof pixels,10000000000ull));b.release(g,surface);
        assert(SUCCEEDED(IDirect3DDevice9_SetRenderState(native,D3DRS_ZENABLE,FALSE)));assert(SUCCEEDED(IDirect3DDevice9_SetRenderState(native,D3DRS_LIGHTING,FALSE)));assert(SUCCEEDED(IDirect3DDevice9_SetRenderState(native,D3DRS_CULLMODE,D3DCULL_NONE)));
        assert(SUCCEEDED(IDirect3DDevice9_SetTexture(native,0,(IDirect3DBaseTexture9*)nt)));assert(SUCCEEDED(IDirect3DDevice9_SetFVF(native,0x104)));
        D3DVIEWPORT9 nv={4,6,20,16,0,1};assert(SUCCEEDED(IDirect3DDevice9_SetViewport(native,&nv)));
        /* Clear with a full viewport, then restore the tested offset viewport. */
        D3DVIEWPORT9 full={0,0,32,32,0,1};assert(SUCCEEDED(IDirect3DDevice9_SetViewport(native,&full)));assert(SUCCEEDED(IDirect3DDevice9_Clear(native,0,NULL,D3DCLEAR_TARGET,0xff203040,1,0)));assert(SUCCEEDED(IDirect3DDevice9_SetViewport(native,&nv)));
        assert(SUCCEEDED(IDirect3DDevice9_BeginScene(native)));HRESULT drawn=frame==1?IDirect3DDevice9_DrawIndexedPrimitiveUP(native,D3DPT_TRIANGLESTRIP,0,4,2,indices,D3DFMT_INDEX16,v,24):IDirect3DDevice9_DrawPrimitiveUP(native,D3DPT_TRIANGLESTRIP,2,v,24);assert(SUCCEEDED(drawn));assert(SUCCEEDED(IDirect3DDevice9_EndScene(native)));
        assert(SUCCEEDED(IDirect3DDevice9_GetRenderTargetData(native,target,readback)));assert(SUCCEEDED(IDirect3DSurface9_LockRect(readback,&lock,NULL,D3DLOCK_READONLY)));
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)for(unsigned c=0;c<3;c++){
            unsigned expected=((unsigned char*)lock.pBits)[y*lock.Pitch+x*4+c],actual=pixels[(y*32+x)*4+c];
            if(actual!=expected)fprintf(stderr,"pixel mismatch frame%u %u,%u c%u got%u expected%u\n",frame,x,y,c,actual,expected);
            assert(actual==expected);checks++;
        }assert(SUCCEEDED(IDirect3DSurface9_UnlockRect(readback)));
        assert(!b.scene(g,1));set(&b,g,137,1);assert(b.draw_up(g,0,5,0,0,2,NULL,0,v,24,sizeof v,0)!=0);set(&b,g,137,0);assert(!b.scene(g,0));
    }
    IDirect3DSurface9_Release(readback);IDirect3DSurface9_Release(target);IDirect3DTexture9_Release(nt);IDirect3DDevice9_Release(native);IDirect3D9_Release(d3d);DestroyWindow(window);
    b.release(g,decl);b.release(g,tex);assert(!fnv_vk_backend_wait(g,10000000000ull));b.device_release(g);assert(!fnv_vk_backend_destroy(g));FreeLibrary(loader);
    for(unsigned i=0;i<2;i++){free(code[i]);free(spv[i]);}printf("{\"status\":\"PASS\",\"native_pixel_channels\":%u,\"reset_pair_retained\":true,\"played_audio\":false}\n",checks);return 0;
}
