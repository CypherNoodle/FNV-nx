static unsigned texture_block_checks;
static void leave_texture_blocks_for_close(void){
    uint32_t a=make_texture(2,2,1,0,21,1),b=make_texture(2,2,1,0,21,1);
    assert(!call(SB_BEGIN,1,&device_at));uint32_t args[]={device_at,3,a};assert(!call(T_BIND,3,args));args[1]=4;assert(!call(T_BIND,3,args));
    (void)end_block();assert(!release(a));assert(!call(SB_BEGIN,1,&device_at));args[2]=b;assert(!call(T_BIND,3,args));assert(!release(b));
    /* Intentionally leave both an owned block and an active recorder alive. */
}
static void block_texture(unsigned sampler,uint32_t texture){uint32_t args[]={device_at,sampler,texture};assert(!call(T_BIND,3,args));}
static void block_texture_is(unsigned sampler,uint32_t texture){uint32_t args[]={device_at,sampler,GUEST_OUT};assert(!call(T_GET,3,args) && get(GUEST_OUT)==texture);if(texture)assert(!release(texture));texture_block_checks++;}
static void exercise_block_textures(uint32_t original){
    uint32_t a=make_texture(2,2,1,0,21,1),b=make_texture(2,2,1,0,21,1);uintptr_t native_b=texture_id;
    const unsigned char gray_a[4]={37,37,37,37},gray_b[4]={93,93,93,93};draw_texture(a,gray_a);draw_texture(b,gray_b);texture_ready(a);texture_ready(b);
    assert(!call(SB_BEGIN,1,&device_at));for(unsigned i=0;i<16;++i)block_texture(i,a);block_texture(15,0);block_texture(0,b);block_texture(0,a);
    /* Before End, current bindings still contain only the original at slot 0. */
    for(unsigned i=0;i<16;++i){uint32_t args[]={device_at,i,GUEST_OUT};assert(!call(T_GET,3,args) && get(GUEST_OUT)==(i?0:original));if(!i)release(original);texture_block_checks++;}
    uint32_t block=end_block();assert(!release(a));assert(!call(SB_APPLY,1,&block));for(unsigned i=0;i<16;++i)block_texture_is(i,i==15?0:a);
    const unsigned char red_a[4]={37,0,0,255},red_b[4]={93,0,0,255};state_draw(4,1,0xff000000,red_a,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    for(unsigned i=0;i<16;++i)block_texture(i,b);assert(!call(SB_CAPTURE,1,&block));for(unsigned i=0;i<16;++i)block_texture(i,0);assert(!release(b));
    assert(!call(SB_APPLY,1,&block));for(unsigned i=0;i<16;++i)block_texture_is(i,b);
    /* Failed transaction must leave texture, numeric state and output intact. */
    struct fnv_d3d9_state_value batch[2]={{.kind=FNV_D3D9_TEXTURE_BINDING,.sampler=0},{.kind=FNV_D3D9_RENDER_STATE,.name=22,.values={99}}};
    assert(provider.state_batch(gpu,batch,2,0)==FNV_VK_INVALID);block_texture_is(0,b);assert(render_value(22)==1);
    assert(!provider.state_batch(gpu,batch,1,2));block_texture_is(0,b);
    batch[1]=(struct fnv_d3d9_state_value){.kind=FNV_D3D9_TEXTURE_BINDING,.sampler=16};struct fnv_d3d9_state_value saved[2];memcpy(saved,batch,sizeof(saved));assert(provider.state_batch(gpu,batch,2,1)==FNV_VK_INVALID && !memcmp(saved,batch,sizeof(saved)));
    batch[0].values[0]=UINT32_MAX;batch[0].values[1]=UINT32_MAX;assert(provider.state_batch(gpu,batch,1,0)==FNV_VK_INVALID);block_texture_is(0,b);
    batch[0].values[0]=batch[0].values[1]=0;assert(!provider.state_batch(gpu,batch,1,1));assert(((uint64_t)batch[0].values[0]|(uint64_t)batch[0].values[1]<<32)==native_b);
    assert(!release(block));state_draw(4,1,0xff000000,red_b,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    for(unsigned i=0;i<16;++i)block_texture(i,0);block_texture(0,original);
}
