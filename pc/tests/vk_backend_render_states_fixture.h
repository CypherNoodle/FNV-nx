static unsigned render_state_pixel_checks;
static void set_render(unsigned name,unsigned value){uint32_t args[]={device_at,name,value};assert(!call(SET_RENDER,3,args));}
static void state_draw_pixels(const unsigned char rgba[4],unsigned tolerance){
    unsigned char pixels[4096];assert(!fnv_vk_backend_read_color(gpu,native_color(),pixels,sizeof(pixels),10000000000ull));
    for(unsigned i=0;i<1024;++i)for(unsigned channel=0;channel<4;++channel){unsigned actual=pixels[i*4+channel],expected=rgba[channel==0?2:channel==2?0:channel];
        if((actual>expected?actual-expected:expected-actual)>tolerance){fprintf(stderr,"state draw pixel i=%u channelBGRA=%u actual=%u expected=%u\n",i,channel,actual,expected);exit(1);}}
    render_state_pixel_checks+=1024;
}
static void state_draw(uint32_t primitive,unsigned indexed,uint32_t background,const unsigned char rgba[4],unsigned tolerance){
    clear_color(background);assert(!call(BEGIN,1,&device_at));
    if(indexed){uint32_t args[]={device_at,primitive,UINT32_MAX,1,4,2,2};assert(!call(D_INDEXED,7,args));}
    else {uint32_t args[]={device_at,primitive,0,2};assert(!call(D_PRIMITIVE,4,args));}
    assert(!call(END,1,&device_at));state_draw_pixels(rgba,tolerance);
}
static void shader_rgba(void){
    const float zero[4]={0},one[4]={1,1,1,1};memcpy(ptr(DATA),zero,16);uint32_t args[]={device_at,3,DATA,1};assert(!call(PS_SF,4,args));
    memcpy(ptr(DATA),one,16);args[1]=12;assert(!call(PS_SF,4,args));
}
static void exercise_render_states(uint32_t vb,uint32_t ib,uint32_t texture,const unsigned char restore[4]){
    set_render(174,0);set_render(7,0);set_render(27,0);draw_mirrored=0;
    uint32_t sampler[]={device_at,0,1,3};assert(!call(D_SAMPLER_SET,4,sampler));
    const unsigned char white[4]={255,255,255,255};draw_texture(texture,white);texture_ready(texture);draw_constants(0,.5f);draw_geometry(vb,ib,0);
    const unsigned char red[4]={255,0,0,255},black[4]={0,0,0,255};
    for(unsigned reverse=0;reverse<2;++reverse){uint16_t indices[8]={0,0,1,2,3,1,3,4};
        if(reverse){indices[3]=3;indices[4]=2;indices[6]=4;indices[7]=3;}upload_buffer(ib,0,16,0,indices);
        for(unsigned mode=1;mode<=3;++mode){set_render(22,mode);unsigned visible=mode==1 || (mode==3 && !reverse) || (mode==2 && reverse);
            state_draw(4,1,0xff000000,visible?red:black,0);}}
    uint32_t get_state[]={device_at,22,GUEST_OUT};assert(!call(GET_RENDER,3,get_state) && get(GUEST_OUT)==3);
    uint32_t invalid[]={device_at,22,4};assert(call(SET_RENDER,3,invalid)==FNV_VK_INVALID);assert(!call(GET_RENDER,3,get_state) && get(GUEST_OUT)==3);set_render(22,1);
    for(unsigned topology=5;topology<=6;++topology){
        draw_geometry(vb,ib,0);uint16_t indices[8]={0,0,1,2,topology==5?4:3,topology==5?3:4};upload_buffer(ib,0,16,0,indices);
        state_draw(topology,1,0xff000000,red,0);
        float fan[16]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,-1,0,1},strip[16]={-1,1,0,0,1,1,1,0,-1,-1,0,1,1,-1,1,1};
        unsigned char data[96]={0};memcpy(data,topology==5?strip:fan,64);upload_buffer(vb,0,96,0,data);
        uint32_t stream[]={device_at,0,vb,0,16};assert(!call(B_STREAM,5,stream));state_draw(topology,0,0xff000000,red,0);
        stream[3]=16;assert(!call(B_STREAM,5,stream));}
    draw_geometry(vb,ib,0);unsigned pitch;unsigned char *pixels=lock_texture(texture,0,0,0,&pitch);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=pixels+y*pitch+x*4;p[0]=32;p[1]=64;p[2]=128;p[3]=128;}
    assert(!unlock_texture(texture,0));texture_ready(texture);shader_rgba();set_render(27,1);set_render(19,2);set_render(20,2);
    const unsigned char operations[5][4]={{192,160,192,160},{64,0,0,96},{0,32,128,0},{64,64,32,32},{128,96,160,128}};
    for(unsigned operation=1;operation<=5;++operation){set_render(171,operation);state_draw(4,1,0x204060a0,operations[operation-1],1);}
    set_render(171,1);set_render(19,5);set_render(20,6);
    const unsigned char alpha_common[4]={96,80,96,80},alpha_separate[4]={96,80,96,128};
    state_draw(4,1,0x204060a0,alpha_common,1);set_render(206,1);set_render(207,2);set_render(208,1);set_render(209,1);state_draw(4,1,0x204060a0,alpha_separate,1);
    set_render(206,0);set_render(19,14);set_render(20,15);set_render(193,0x204080c0);const unsigned char constant[4]={80,80,64,44};state_draw(4,1,0x204060a0,constant,1);
    set_render(27,0);set_render(168,5);const unsigned char mask[4]={128,96,32,32};state_draw(4,1,0x204060a0,mask,0);
    set_render(168,15);set_render(27,1);set_render(19,11);set_render(20,1);const unsigned char saturated[4]={64,32,16,128};state_draw(4,1,0x204060a0,saturated,1);
    uint32_t get_blend[]={device_at,19,GUEST_OUT};assert(!call(GET_RENDER,3,get_blend) && get(GUEST_OUT)==11);invalid[1]=19;invalid[2]=12;
    assert(call(SET_RENDER,3,invalid)==FNV_VK_INVALID);assert(!call(GET_RENDER,3,get_blend) && get(GUEST_OUT)==11);
    set_render(27,0);set_render(19,2);set_render(20,1);set_render(171,1);set_render(193,0xffffffff);set_render(22,1);
    sampler[3]=2;assert(!call(D_SAMPLER_SET,4,sampler));draw_mirrored=1;draw_texture(texture,restore);texture_ready(texture);
}
