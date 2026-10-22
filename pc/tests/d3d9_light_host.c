/* Compare CPU light bank with Windows HAL state; no lighting pixel claims. */
#include <windows.h>
#include <d3d9.h>
#include "d3d9_light_state.h"
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
_Static_assert(sizeof(D3DLIGHT9)==104,"D3DLIGHT9 ABI");
static unsigned checks;
static void compare(IDirect3DDevice9 *d,struct fnv_light_bank *bank,uint32_t id){
    struct fnv_light *l=fnv_light_find(bank,id);assert(l);D3DLIGHT9 actual;BOOL enabled=0;
    assert(!d->lpVtbl->GetLight(d,id,&actual) && !memcmp(&actual,l->words,104));
    assert(!d->lpVtbl->GetLightEnable(d,id,&enabled) && (uint32_t)enabled==l->enabled);checks++;
}
int main(void){
    HWND w=CreateWindowExA(0,"STATIC","Light oracle",WS_POPUP,0,0,32,32,NULL,NULL,GetModuleHandleA(NULL),NULL);assert(w);
    IDirect3D9 *api=Direct3DCreate9(32);assert(api);D3DPRESENT_PARAMETERS p={0};p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;
    IDirect3DDevice9*d=NULL;assert(!api->lpVtbl->CreateDevice(api,0,D3DDEVTYPE_HAL,w,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&d));
    struct fnv_light_bank bank={0},record={0};D3DLIGHT9 value={0},actual;BOOL enabled=0;uint32_t off=0,on=0xffffffff;
    assert(d->lpVtbl->GetLight(d,9,&actual)==D3DERR_INVALIDCALL && !fnv_light_find(&bank,9));
    assert(!d->lpVtbl->LightEnable(d,9,FALSE) && !fnv_light_change(&bank,NULL,9,NULL,&off));compare(d,&bank,9);
    for(unsigned i=0;i<300;i++){
        uint32_t id=i*7u+32u;float values[26];for(unsigned j=0;j<26;j++)values[j]=(float)((int)((i+j*3)%71)-35)*.25f;
        memcpy(&value,values,104);value.Type=(D3DLIGHTTYPE)(i%5);uint32_t words[26];memcpy(words,&value,104);
        assert(!d->lpVtbl->SetLight(d,id,&value) && !fnv_light_change(&bank,NULL,id,words,NULL));compare(d,&bank,id);
        assert(!d->lpVtbl->LightEnable(d,id,TRUE) && !fnv_light_change(&bank,NULL,id,NULL,&on));compare(d,&bank,id);
    }
    uint32_t id=3000;value.Type=D3DLIGHT_DIRECTIONAL;value.Direction.z=2;uint32_t words[26];memcpy(words,&value,104);
    assert(!d->lpVtbl->SetLight(d,id,&value) && !fnv_light_change(&bank,NULL,id,words,NULL));
    assert(!d->lpVtbl->BeginStateBlock(d));value.Direction.z=5;memcpy(words,&value,104);
    assert(!d->lpVtbl->SetLight(d,id,&value) && !fnv_light_change(&bank,&record,id,words,NULL));
    assert(!d->lpVtbl->LightEnable(d,id,TRUE) && !fnv_light_change(&bank,&record,id,NULL,&on));compare(d,&bank,id);
    /* New recorded indices publish default live lights, not recorded values. */
    uint32_t fresh=4000;assert(!d->lpVtbl->SetLight(d,fresh,&value) && !fnv_light_change(&bank,&record,fresh,words,NULL));compare(d,&bank,fresh);
    assert(!d->lpVtbl->LightEnable(d,fresh-1,FALSE) && !fnv_light_change(&bank,&record,fresh-1,NULL,&off));compare(d,&bank,fresh-1);
    IDirect3DStateBlock9 *block=NULL;assert(!d->lpVtbl->EndStateBlock(d,&block));
    assert(!fnv_light_block_prepare(&bank,&record,0) && !block->lpVtbl->Apply(block));fnv_light_block_commit(&bank,&record,0);
    compare(d,&bank,id);compare(d,&bank,fresh);compare(d,&bank,fresh-1);
    value.Direction.z=7;memcpy(words,&value,104);assert(!d->lpVtbl->SetLight(d,id,&value) && !fnv_light_change(&bank,NULL,id,words,NULL));
    assert(!d->lpVtbl->LightEnable(d,id,FALSE) && !fnv_light_change(&bank,NULL,id,NULL,&off));
    assert(!fnv_light_block_prepare(&bank,&record,1) && !block->lpVtbl->Capture(block));fnv_light_block_commit(&bank,&record,1);
    value.Direction.z=8;memcpy(words,&value,104);assert(!d->lpVtbl->SetLight(d,id,&value) && !fnv_light_change(&bank,NULL,id,words,NULL));
    assert(!fnv_light_block_prepare(&bank,&record,0) && !block->lpVtbl->Apply(block));fnv_light_block_commit(&bank,&record,0);compare(d,&bank,id);
    assert(!d->lpVtbl->GetLightEnable(d,id,&enabled) && enabled==0);
    block->lpVtbl->Release(block);fnv_light_destroy(&record);
    memset(&p,0,sizeof(p));p.Windowed=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=w;assert(!d->lpVtbl->Reset(d,&p));bank.count=0;
    assert(d->lpVtbl->GetLight(d,id,&actual)==D3DERR_INVALIDCALL && !fnv_light_find(&bank,id));checks++;
    fnv_light_destroy(&bank);d->lpVtbl->Release(d);api->lpVtbl->Release(api);assert(DestroyWindow(w));
    printf("{\"status\":\"PASS\",\"light_comparisons\":%u,\"windows_hal\":true,\"gpu_lighting_tested\":false}\n",checks);return 0;
}
