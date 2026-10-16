#define VK_NO_PROTOTYPES
#include "d3d9_vk_state.h"
enum {S_RENDER=37,S_GET_RENDER,S_SAMPLER,S_GET_SAMPLER};
static struct fnv_d3d9_depth state_depth,depth_variants[2];
static struct fnv_d3d9_sampler state_sampler,sampler_snapshots[3];
static unsigned state_snapshots,state_rejected;
static void state_sampler_vk(unsigned index,float cap,VkSamplerCreateInfo *out){
    assert(!fnv_d3d9_vk_sampler(sampler_snapshots+index,1,cap,out));
    /* Negative control exists only in this test fixture. Forcing clamp must
     * fail the out-of-range UV pixel check; never used by native code. */
    if(getenv("FNV_TEST_FORCE_CLAMP"))out->addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}
static uint32_t state_provider(void *ctx,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,uint32_t values[6],int get_state){
    (void)ctx;
    if(kind==FNV_D3D9_RENDER_STATE){
        unsigned index=name==7?0:name==14?1:name==23?2:3;if(index==3)return FNV_D3D9_INVALIDCALL;
        uint32_t raw[3];memcpy(raw,&state_depth,sizeof(raw));if(get_state){values[0]=raw[index];return 0;}
        raw[index]=values[0];struct fnv_d3d9_depth next;memcpy(&next,raw,sizeof(next));VkPipelineDepthStencilStateCreateInfo info;
        uint32_t hr=fnv_d3d9_vk_depth(&next,&info);if(hr){state_rejected++;return hr;}
        HRESULT r=IDirect3DDevice9_SetRenderState(device,name,values[0]);if(SUCCEEDED(r))state_depth=next;return (uint32_t)r;
    }
    if(kind==FNV_D3D9_SAMPLER_STATE && !sampler && name>=1 && name<=10){
        uint32_t raw[10];memcpy(raw,&state_sampler,sizeof(raw));if(get_state){values[0]=raw[name-1];return 0;}
        raw[name-1]=values[0];struct fnv_d3d9_sampler next;memcpy(&next,raw,sizeof(next));VkSamplerCreateInfo info;
        /* Fixture accepts bias zero only. Real creation revalidates against
         * the selected physical device's actual maxSamplerLodBias. */
        uint32_t hr=fnv_d3d9_vk_sampler(&next,1,0,&info);if(hr){state_rejected++;return hr;}
        HRESULT r=IDirect3DDevice9_SetSamplerState(device,0,name,values[0]);if(SUCCEEDED(r))state_sampler=next;return (uint32_t)r;
    }
    return FNV_D3D9_INVALIDCALL;
}
static void state_render(uint32_t name,uint32_t value){uint32_t args[]={device_at,name,value};assert(!call(S_RENDER,3,args));}
static void state_sampling(uint32_t name,uint32_t value){uint32_t args[]={device_at,0,name,value};assert(!call(S_SAMPLER,4,args));}
static void state_setup(void){
    _Static_assert(sizeof(state_depth)==12 && sizeof(state_sampler)==40,"packed scalar state");
    uint32_t depth[3],sampler[10];const unsigned names[3]={7,14,23};
    for(unsigned i=0;i<3;++i)assert(SUCCEEDED(IDirect3DDevice9_GetRenderState(device,names[i],(DWORD*)(depth+i))));
    for(unsigned i=0;i<10;++i)assert(SUCCEEDED(IDirect3DDevice9_GetSamplerState(device,0,i+1,(DWORD*)(sampler+i))));
    memcpy(&state_depth,depth,12);memcpy(&state_sampler,sampler,40);
    state_render(7,1);state_render(14,1);state_render(23,4);
    state_sampling(1,3);state_sampling(2,3);state_sampling(3,3);
    state_sampling(5,1);state_sampling(6,1);state_sampling(7,0);
    uint32_t invalid[]={device_at,0,5,0};assert(call(S_SAMPLER,4,invalid)==FNV_D3D9_INVALIDCALL);
    invalid[3]=GUEST_OUT;assert(!call(S_GET_SAMPLER,4,invalid) && get(GUEST_OUT)==1);
    uint32_t bad_depth[]={device_at,7,2};assert(call(S_RENDER,3,bad_depth)==FNV_D3D9_INVALIDCALL);
    bad_depth[2]=GUEST_OUT;assert(!call(S_GET_RENDER,3,bad_depth) && get(GUEST_OUT)==1);
}
static void state_frame(unsigned frame){state_sampling(1,frame==0?3:frame==1?1:2);}
static void state_record(void){
    assert(state_snapshots<3 && state_depth.enable==1 && state_depth.write==1 && state_depth.compare==4);
    sampler_snapshots[state_snapshots]=state_sampler;
    if(!state_snapshots)depth_variants[0]=state_depth;
    state_snapshots++;
}
static void state_depth_disabled(void){
    /* This separate Vulkan pass is configured through COM setters. It is not
     * a fourth COM draw: the fixture explicitly chooses the two variants. */
    state_render(7,0);state_render(14,0);depth_variants[1]=state_depth;
    assert(state_snapshots==3 && state_rejected==2 && depth_variants[0].enable && !depth_variants[1].enable);
    state_sampling(1,3);assert(sampler_snapshots[1].address_u==1 && sampler_snapshots[2].address_u==2);
}
