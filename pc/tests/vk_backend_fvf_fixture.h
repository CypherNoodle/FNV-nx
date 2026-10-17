enum {FVF_SET=86,FVF_GET};
static unsigned fvf_checks,fvf_pixel_checks;
static unsigned current_fvf(void){uint32_t args[]={device_at,GUEST_OUT};assert(!call(FVF_GET,2,args));return get(GUEST_OUT);}
static void set_fvf(unsigned fvf){uint32_t args[]={device_at,fvf};assert(!call(FVF_SET,2,args));assert(current_fvf()==fvf);fvf_checks++;}
static void exercise_fvf(uint32_t vb,uint32_t ib,uint32_t declaration,uint32_t texture){
    assert(!current_fvf());uintptr_t blockers[32];unsigned count=0;const unsigned char empty[8]={255,0,0,0,17,0,0,0};
    while(count<32 && !provider.create_declaration(gpu,empty,1,blockers+count))count++;
    assert(count<32);uint32_t attempt[]={device_at,0x102};assert(call(FVF_SET,2,attempt)==FNV_VK_OOM && !current_fvf());
    while(count)provider.release(gpu,blockers[--count]);fvf_checks++;
    const unsigned formats[12]={0x102,0x112,0x152,0x1d2,0x104,0x4002,0x100,0,0x20102,0x30102,0x10102,0x802};
    for(unsigned i=0;i<12;++i){set_fvf(formats[i]);struct fnv_fvf_layout expected;assert(!fnv_d3d9_fvf_layout(formats[i],&expected));
        uint32_t get_decl[]={device_at,GUEST_OUT};assert(!call(S_DECL_GET,2,get_decl));uint32_t decl=get(GUEST_OUT);assert(decl);
        uint32_t get_elements[]={decl,DATA,GUEST_OUT+4};put(GUEST_OUT+4,16);assert(!call(S_DECL_ELEMENTS,3,get_elements) && get(GUEST_OUT+4)==expected.count && !memcmp(ptr(DATA),expected.elements,expected.count*8));
        uint32_t bind[]={device_at,decl};assert(!call(S_DECL_BIND,2,bind) && current_fvf()==formats[i]);assert(!release(decl));fvf_checks+=2;
    }
    set_fvf(0x102);uint32_t get_decl[]={device_at,GUEST_OUT};assert(!call(S_DECL_GET,2,get_decl));uint32_t held=get(GUEST_OUT);set_fvf(0x112);
    uint32_t bind[]={device_at,held};assert(!call(S_DECL_BIND,2,bind) && current_fvf()==0x102);assert(!release(held));fvf_checks++;
    attempt[1]=6;assert(call(FVF_SET,2,attempt)==FNV_VK_INVALID && current_fvf()==0x102);fvf_checks++;
    bind[1]=declaration;assert(!call(S_DECL_BIND,2,bind) && !current_fvf());fvf_checks++;
    set_fvf(0x102);set_render(174,0);set_render(7,0);set_render(27,0);set_render(22,1);draw_constants(0,.5f);
    const unsigned char white[4]={255,255,255,255};draw_texture(texture,white);texture_ready(texture);
    const float vertices[30]={-1,1,9,0,0,1,1,9,1,0,1,-1,9,1,1,-1,1,9,0,0,1,-1,9,1,1,-1,-1,9,0,1};memcpy(ptr(DATA+512),vertices,sizeof(vertices));
    clear_color(0xff000000);assert(!call(BEGIN,1,&device_at));uint32_t draw[]={device_at,4,2,DATA+512,20};assert(!call(U_DRAW,5,draw));assert(!call(END,1,&device_at));
    const unsigned char red[4]={255,0,0,255};state_draw_pixels(red,0);render_state_pixel_checks-=1024;fvf_pixel_checks=1024;
    bind[1]=declaration;assert(!call(S_DECL_BIND,2,bind) && !current_fvf());up_bind_original(vb,ib);draw_geometry(vb,ib,1);draw_mirrored=1;
    const unsigned char restore[4]={21,67,139,233};draw_texture(texture,restore);texture_ready(texture);
}
