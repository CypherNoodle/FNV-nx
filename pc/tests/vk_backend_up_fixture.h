enum {U_DRAW=84,U_INDEXED};
static unsigned up_pixel_checks,up_checks;
static void up_bind_original(uint32_t vb,uint32_t ib){uint32_t stream[]={device_at,0,vb,16,16},index[]={device_at,ib};assert(!call(B_STREAM,5,stream));assert(!call(B_INDICES,2,index));}
static void up_bindings_cleared(unsigned indexed,uint32_t expected_index){
    uint32_t stream[]={device_at,0,GUEST_OUT,GUEST_OUT+4,GUEST_OUT+8};assert(!call(B_GET_STREAM,5,stream) && !get(GUEST_OUT) && !get(GUEST_OUT+4) && !get(GUEST_OUT+8));
    uint32_t index[]={device_at,GUEST_OUT};assert(!call(B_GET_INDICES,2,index) && get(GUEST_OUT)==(indexed?0:expected_index));if(get(GUEST_OUT))assert(release(get(GUEST_OUT))==1);up_checks+=2;
}
static void up_pixels(void){const unsigned char red[4]={255,0,0,255};state_draw_pixels(red,0);up_pixel_checks+=1024;render_state_pixel_checks-=1024;}
static void exercise_up(uint32_t vb,uint32_t ib,uint32_t texture){
    set_render(174,0);set_render(7,0);set_render(27,0);set_render(22,1);draw_constants(0,.5f);
    const unsigned char white[4]={255,255,255,255};draw_texture(texture,white);texture_ready(texture);
    enum {VERTEX_DATA=DATA+512,INDEX_DATA=DATA+1024};
    const float corners[16]={-1,1,0,0,1,1,1,0,1,-1,1,1,-1,-1,0,1};
    uint32_t args[]={device_at,4,1,4,2,INDEX_DATA,101,VERTEX_DATA,16};uint32_t direct[]={device_at,4,2,VERTEX_DATA,16};
    for(unsigned mode=0;mode<6;++mode){unsigned indexed=mode>=3,primitive=4+mode%3;up_bind_original(vb,ib);
        const unsigned order[3][6]={{0,1,2,0,2,3},{0,1,3,2,0,0},{0,1,2,3,0,0}};unsigned count=primitive==4?6:4;
        memset(ptr(VERTEX_DATA),0xa5,128);memset(ptr(INDEX_DATA),0,32);
        if(indexed){memcpy(ptr(VERTEX_DATA)+16,corners,64);for(unsigned i=0;i<count;++i){uint32_t value=order[primitive-4][i]+1;if(mode==4)memcpy(ptr(INDEX_DATA)+i*4,&value,4);else {uint16_t v=(uint16_t)value;memcpy(ptr(INDEX_DATA)+i*2,&v,2);}}}
        else for(unsigned i=0;i<count;++i)memcpy(ptr(VERTEX_DATA)+i*16,corners+order[primitive-4][i]*4,16);
        args[1]=direct[1]=primitive;args[6]=mode==4?102:101;clear_color(0xff000000);assert(!call(BEGIN,1,&device_at));
        assert(!call(indexed?U_INDEXED:U_DRAW,indexed?9:5,indexed?args:direct));
        memset(ptr(VERTEX_DATA),0,128);memset(ptr(INDEX_DATA),0xff,32); /* Access must already be complete. */
        up_bindings_cleared(indexed,ib);assert(!call(END,1,&device_at));up_pixels();
    }
    args[1]=4;args[6]=101;const uint16_t indices[6]={1,2,3,1,3,4};memcpy(ptr(VERTEX_DATA)+16,corners,64);memcpy(ptr(INDEX_DATA),indices,12);
    assert(!call(BEGIN,1,&device_at));
    for(unsigned fault=0;fault<2;++fault){up_bind_original(vb,ib);uint64_t before=stats().allocated_bytes;
        if(!fault)fail_memory=1;else fail_map=1;assert(call(U_INDEXED,9,args)==FNV_VK_OOM && stats().allocated_bytes==before);up_bindings_cleared(1,ib);up_checks++;}
    /* Index range invalidity is checked before recording GPU commands. */
    put(INDEX_DATA,0);assert(call(U_INDEXED,9,args)==FNV_VK_INVALID);memcpy(ptr(INDEX_DATA),indices,12);up_checks++;
    uint32_t invalid[5]={device_at,4,2,BASE+ARENA-8,16};assert(call(U_DRAW,5,invalid)==FNV_VK_INVALID);invalid[3]=VERTEX_DATA;invalid[4]=0;assert(call(U_DRAW,5,invalid)==FNV_VK_INVALID);up_checks+=2;
    /* Two immutable UP versions may remain pending; a third draw reports BUSY. */
    assert(!call(U_INDEXED,9,args));assert(!call(U_INDEXED,9,args));assert(call(U_INDEXED,9,args)==FNV_VK_BUSY);up_checks++;
    memset(ptr(VERTEX_DATA),0,128);memset(ptr(INDEX_DATA),0,32);assert(!call(END,1,&device_at));up_pixels();
    up_bind_original(vb,ib);draw_geometry(vb,ib,1);draw_mirrored=1;
    const unsigned char restore[4]={21,67,139,233};draw_texture(texture,restore);texture_ready(texture);
}
