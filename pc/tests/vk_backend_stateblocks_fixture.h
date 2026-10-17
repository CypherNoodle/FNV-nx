enum {SB_BEGIN=88,SB_END,SB_CAPTURE,SB_APPLY,SB_CREATE};
static unsigned stateblock_checks,stateblock_pixel_checks;
static unsigned render_value(unsigned name){uint32_t args[]={device_at,name,GUEST_OUT};assert(!call(GET_RENDER,3,args));return get(GUEST_OUT);}
static uint32_t end_block(void){uint32_t args[]={device_at,GUEST_OUT};assert(!call(SB_END,2,args));return get(GUEST_OUT);}
static void block_constant_get(unsigned method,unsigned reg,uint32_t data[4]){
    uint32_t args[]={device_at,reg,DATA,1};assert(!call(method,4,args));memcpy(data,ptr(DATA),16);
}
static void exercise_block_constants(void){
    const unsigned methods[6]={VS_SF,VS_SI,VS_SB,PS_SF,PS_SI,PS_SB},regs[6]={7,2,3,12,5,6};
    uint32_t before[6][4],expected[6][4],actual[4];
    for(unsigned i=0;i<6;++i)block_constant_get(methods[i]+1,regs[i],before[i]);
    assert(!call(SB_BEGIN,1,&device_at));draw_constants(2,.25f);draw_constants(0,.5f);
    const uint32_t ints[4]={19,23,29,31};memcpy(ptr(DATA),ints,16);uint32_t args[]={device_at,5,DATA,1};assert(!call(PS_SI,4,args));put(DATA,1);args[1]=6;assert(!call(PS_SB,4,args));memset(ptr(DATA),0xa5,16);
    for(unsigned i=0;i<6;++i){block_constant_get(methods[i]+1,regs[i],actual);assert(!memcmp(actual,before[i],i%3==2?4:16));}
    uint32_t block=end_block();draw_constants(2,.75f);assert(!call(SB_APPLY,1,&block));
    block_constant_get(PS_GI,5,actual);assert(!memcmp(actual,ints,16));block_constant_get(PS_GB,6,actual);assert(actual[0]==1);
    const unsigned char red[4]={255,0,0,255},green[4]={0,255,0,255};state_draw(4,1,0xff000000,red,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    draw_constants(1,.5f);for(unsigned i=0;i<6;++i)block_constant_get(methods[i]+1,regs[i],expected[i]);assert(!call(SB_CAPTURE,1,&block));draw_constants(2,.75f);
    uint32_t marker[4]={0x3f000000,0,0,0};memcpy(ptr(DATA),marker,16);args[1]=20;assert(!call(PS_SF,4,args));
    assert(!call(SB_APPLY,1,&block));for(unsigned i=0;i<6;++i){block_constant_get(methods[i]+1,regs[i],actual);assert(!memcmp(actual,expected[i],i%3==2?4:16));}
    block_constant_get(PS_GF,20,actual);assert(!memcmp(actual,marker,16));state_draw(4,1,0xff000000,green,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    assert(!release(block));draw_constants(0,.5f);stateblock_checks+=5;
}
#include "vk_backend_stateblock_textures_fixture.h"
static void exercise_stateblocks(uint32_t vb,uint32_t ib,uint32_t texture){
    const unsigned char white[4]={255,255,255,255};draw_texture(texture,white);assert(!fnv_vk_backend_wait(gpu,10000000000ull));
    up_bind_original(vb,ib);draw_geometry(vb,ib,0);draw_constants(0,.5f);draw_mirrored=0;set_render(7,0);set_render(174,0);set_render(22,1);set_render(168,15);set_render(27,0);
    uint64_t parents=stats().device_references;assert(!call(SB_BEGIN,1,&device_at));assert(call(SB_BEGIN,1,&device_at)==FNV_VK_INVALID);stateblock_checks++;
    set_render(168,4);set_render(168,0);set_render(22,1);uint32_t sampler[]={device_at,0,1,3};assert(!call(D_SAMPLER_SET,4,sampler));
    const uint32_t viewport[6]={0,0,32,32,0,0x3f800000},scissor[4]={4,4,12,12};memcpy(ptr(DATA),viewport,24);uint32_t region[]={device_at,DATA};assert(!call(SET_VIEW,2,region));memcpy(ptr(DATA),scissor,16);assert(!call(SET_SCISSOR,2,region));
    assert(render_value(168)==15);stateblock_checks++;uint32_t block=end_block();assert(block && render_value(168)==15 && stats().device_references==parents+1);stateblock_checks++;
    const unsigned char iid[16]={0xe5,0x4f,0x7c,0xb0,0x0d,0x31,0xa8,0x4b,0xa2,0x3c,0x4f,0x0f,0x20,0x6f,0x21,0x8b};memcpy(ptr(DATA),iid,16);
    uint32_t query[]={block,DATA,GUEST_OUT};assert(!call(QUERY,3,query) && get(GUEST_OUT)==block && release(block)==1 && stats().device_references==parents+1);stateblock_checks++;
    uint32_t get_parent[]={block,GUEST_OUT};assert(!call(GET_DEVICE,2,get_parent) && get(GUEST_OUT)==device_at);assert(release(device_at)>0);stateblock_checks++;
    assert(!call(SB_APPLY,1,&block) && render_value(168)==0);stateblock_checks++;
    const unsigned char black[4]={0,0,0,255},red[4]={255,0,0,255};
    /* Establish a known background before testing disabled draw writes. */
    set_render(168,15);clear_color(0xff000000);assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));assert(!call(SB_APPLY,1,&block));
    assert(!call(BEGIN,1,&device_at));uint32_t draw[]={device_at,4,UINT32_MAX,1,4,2,2};assert(!call(D_INDEXED,7,draw));assert(!call(END,1,&device_at));state_draw_pixels(black,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    set_render(168,15);assert(!call(SB_CAPTURE,1,&block));set_render(168,0);set_render(22,2);assert(!call(SB_APPLY,1,&block) && render_value(168)==15 && render_value(22)==1);stateblock_checks++;
    assert(!call(BEGIN,1,&device_at));assert(!call(D_INDEXED,7,draw));assert(!call(END,1,&device_at));state_draw_pixels(red,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    exercise_block_constants();
    exercise_block_textures(texture);
    assert(!call(SB_BEGIN,1,&device_at));assert(call(SB_CAPTURE,1,&block)==FNV_VK_INVALID && call(SB_APPLY,1,&block)==FNV_VK_INVALID);uint32_t empty=end_block();assert(!call(SB_CAPTURE,1,&empty) && !call(SB_APPLY,1,&empty) && !release(empty));stateblock_checks+=2;
    assert(!call(SB_BEGIN,1,&device_at));
    uint32_t texture_args[]={device_at,0,texture};assert(!call(T_BIND,3,texture_args));uint32_t unsupported_shader[]={device_at,0};assert(call(S_VS_BIND,2,unsupported_shader)==0x8876086au);uint32_t end[]={device_at,GUEST_OUT};put(GUEST_OUT,0xa5);assert(call(SB_END,2,end)==0x8876086au && !get(GUEST_OUT));stateblock_checks++;
    uint32_t create[]={device_at,1,GUEST_OUT};assert(call(SB_CREATE,3,create)==0x8876086au && !get(GUEST_OUT));stateblock_checks++;
    const unsigned unsupported_methods[7]={S_VS_BIND,S_PS_BIND,S_DECL_BIND,B_STREAM,B_INDICES,B_FREQ,FVF_SET};
    const unsigned argument_counts[7]={2,2,2,5,2,3,2};
    uint32_t arguments[7][5]={{device_at,0},{device_at,0},{device_at,0},{device_at,0,vb,16,16},{device_at,ib},{device_at,0,1},{device_at,0x102}};
    for(unsigned i=0;i<7;++i){assert(!call(SB_BEGIN,1,&device_at));set_render(168,0);assert(call(unsupported_methods[i],argument_counts[i],arguments[i])==0x8876086au);
        assert(call(SB_END,2,end)==0x8876086au && !get(GUEST_OUT) && render_value(168)==15);stateblock_checks++;}
    /* Capacity failure keeps the recorder available for a retry. */
    uint32_t held[32];unsigned held_count=0;
    while(held_count<32){assert(!call(SB_BEGIN,1,&device_at));uint32_t hr=call(SB_END,2,end);if(hr){assert(hr==FNV_VK_OOM && !get(GUEST_OUT));break;}held[held_count++]=get(GUEST_OUT);}
    assert(held_count && held_count<32);assert(!release(held[--held_count]));uint32_t retried=end_block();assert(!release(retried));
    while(held_count){assert(!release(held[--held_count]));}
    assert(stats().device_references==parents+1);stateblock_checks+=2;
    struct fnv_d3d9_state_value batch[2]={{.kind=FNV_D3D9_RENDER_STATE,.name=168,.values={0}},{.kind=FNV_D3D9_RENDER_STATE,.name=22,.values={99}}};
    assert(provider.state_batch(gpu,batch,2,0)==FNV_VK_INVALID && render_value(168)==15 && render_value(22)==1);stateblock_checks++;
    assert(!provider.state_batch(gpu,batch,1,2) && render_value(168)==15);stateblock_checks++;
    struct fnv_d3d9_state_value saved[2];memcpy(saved,batch,sizeof(saved));batch[1].name=0xffffffffu;memcpy(saved,batch,sizeof(saved));assert(provider.state_batch(gpu,batch,2,1)==FNV_VK_INVALID && !memcmp(saved,batch,sizeof(saved)));stateblock_checks++;
    assert(!release(block) && stats().device_references==parents);stateblock_checks++;
    sampler[3]=2;assert(!call(D_SAMPLER_SET,4,sampler));draw_geometry(vb,ib,1);draw_mirrored=1;
    const unsigned char restore[4]={21,67,139,233};draw_texture(texture,restore);assert(!fnv_vk_backend_wait(gpu,10000000000ull));
}
