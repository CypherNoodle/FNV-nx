static unsigned shader_block_checks;
static void check_uniform_binding_transaction(void){
    struct fnv_uniform_state *s;struct fnv_uniform_config config={256,224,2,16};assert(!fnv_uniform_state_create(&config,&s));
    struct fnv_uniform_plan good={16,0,NULL},bad={32,0,NULL};const struct fnv_uniform_plan *plans[2]={&good,&good};assert(!fnv_uniform_state_bind_batch(s,3,plans,0));
    plans[0]=NULL;plans[1]=&bad;assert(fnv_uniform_state_bind_batch(s,3,plans,0)==FNV_VK_INVALID);
    struct fnv_uniform_draw draw;assert(!fnv_uniform_state_prepare(s,&draw) && draw.bytes[0]==16 && draw.bytes[1]==16);assert(!fnv_uniform_state_cancel(s,draw.ticket));
    plans[1]=NULL;assert(!fnv_uniform_state_bind_batch(s,3,plans,1));assert(!fnv_uniform_state_prepare(s,&draw));assert(!fnv_uniform_state_cancel(s,draw.ticket));
    assert(!fnv_uniform_state_bind_batch(s,1,plans,0));assert(fnv_uniform_state_prepare(s,&draw)==FNV_VK_INVALID);assert(!fnv_uniform_state_destroy(s));
}
static uint32_t block_shader_get(unsigned stage){uint32_t a[]={device_at,GUEST_OUT};assert(!call(stage?S_PS_GET:S_VS_GET,2,a));return get(GUEST_OUT);}
static void block_shader_is(unsigned stage,uint32_t expected){uint32_t id=block_shader_get(stage);assert(id==expected);if(id)release(id);shader_block_checks++;}
static void block_shader_set(unsigned stage,uint32_t id){uint32_t a[]={device_at,id};assert(!call(stage?S_PS_BIND:S_VS_BIND,2,a));}
static void exercise_block_shaders(void){
    check_uniform_binding_transaction();
    uint32_t original[2]={block_shader_get(0),block_shader_get(1)},created[2]={0};
    for(unsigned i=0;i<catalog_count;++i)created[((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff]=make_shader(i);
    assert(!call(SB_BEGIN,1,&device_at));block_shader_set(0,created[0]);block_shader_set(1,0);block_shader_set(1,created[1]);
    block_shader_is(0,original[0]);block_shader_is(1,original[1]);uint32_t block=end_block();assert(!release(created[0]) && !release(created[1]));
    block_shader_set(0,0);block_shader_set(1,0);assert(!call(SB_APPLY,1,&block));block_shader_is(0,created[0]);block_shader_is(1,created[1]);
    const unsigned char red[4]={255,0,0,255};state_draw(4,1,0xff000000,red,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;
    struct fnv_d3d9_state_value batch[2]={{.kind=FNV_D3D9_SHADER_BINDING,.sampler=0},{.kind=FNV_D3D9_RENDER_STATE,.name=22,.values={99}}};
    assert(provider.state_batch(gpu,batch,2,0)==FNV_VK_INVALID && render_value(22)==1);block_shader_is(0,created[0]);block_shader_is(1,created[1]);
    block_shader_set(0,0);block_shader_set(1,0);assert(!call(SB_CAPTURE,1,&block));block_shader_set(0,original[0]);block_shader_set(1,original[1]);assert(!call(SB_APPLY,1,&block));block_shader_is(0,0);block_shader_is(1,0);assert(!release(block));
    block_shader_set(0,original[0]);block_shader_set(1,original[1]);
    uint32_t single=0;for(unsigned i=0;i<catalog_count;++i)if(((const uint32_t*)catalog[i].bytes)[0]>>16==0xfffe)single=make_shader(i);
    assert(single && !call(SB_BEGIN,1,&device_at));block_shader_set(0,single);block=end_block();assert(!release(single));assert(!call(SB_APPLY,1,&block));block_shader_is(0,single);block_shader_is(1,original[1]);
    state_draw(4,1,0xff000000,red,0);stateblock_pixel_checks+=1024;render_state_pixel_checks-=1024;block_shader_set(0,original[0]);assert(!release(block));release(original[0]);release(original[1]);
}
