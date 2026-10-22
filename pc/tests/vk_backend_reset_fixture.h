enum {F_RESET=82,F_COOPERATIVE};
static unsigned reset_checks;
static const uint32_t reset_params[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};
static uint32_t reset_call(void){memcpy(ptr(DATA),reset_params,56);uint32_t a[]={device_at,DATA};return call(F_RESET,2,a);}
static DWORD WINAPI reset_other_thread(void *unused){
    (void)unused;stack_offset=8192;struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-stack_offset,0);assert(!fnv_cpu_attach(cpu,&initial));
    assert(call(F_COOPERATIVE,1,&device_at)==FNV_VK_INVALID);uint32_t args[]={device_at,DATA};assert(call(F_RESET,2,args)==FNV_VK_INVALID);assert(!fnv_cpu_detach());return 0;
}
static void exercise_reset(void){
    uint32_t mat[17]={0},mat_out[17];mat[0]=0x3f000000;material_set(mat);
    float matrix[16],matrix_out[16],identity[16];matrix_identity(identity);
    memcpy(matrix,identity,64);matrix[12]=77;
    assert(!call(SB_BEGIN,1,&device_at));matrix_set(256,matrix);uint32_t matrix_block=end_block();
    matrix[12]=19;matrix_set(256,matrix);
    assert(!call(F_COOPERATIVE,1,&device_at));reset_checks++;
    memcpy(ptr(DATA),reset_params,56);put(DATA,64);unsigned char saved[56];memcpy(saved,ptr(DATA),56);uint32_t args[]={device_at,DATA};
    assert(call(F_RESET,2,args)==FNV_VK_INVALID && !memcmp(ptr(DATA),saved,56));assert(call(F_COOPERATIVE,1,&device_at)==0x88760869u);reset_checks+=2;
    material_get(mat_out);assert(!memcmp(mat,mat_out,68));
    matrix_get(256,matrix_out);assert(!memcmp(matrix,matrix_out,64));
    assert(!reset_call() && get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==21 && get(DATA+12)==1);assert(!call(F_COOPERATIVE,1,&device_at));reset_checks+=2;
    memset(mat,0,68);material_get(mat_out);assert(!memcmp(mat,mat_out,68));
    for(unsigned state=0;state<512;state++)if(fnv_transform_index(state)>=0){matrix_get(state,matrix_out);assert(!memcmp(identity,matrix_out,64));}
    assert(!call(SB_APPLY,1,&matrix_block));matrix_get(256,matrix_out);assert(matrix_out[12]==77);assert(!release(matrix_block));matrix_set(256,identity);
    uint32_t rt[]={device_at,0,GUEST_OUT};assert(!call(GET_RT,3,rt));uint32_t back=get(GUEST_OUT);
    assert(reset_call()==FNV_VK_INVALID);assert(call(F_COOPERATIVE,1,&device_at)==0x88760869u);assert(!release(back));assert(!reset_call());reset_checks+=2;
    uint32_t def=make_buffer(0,16,0,0,0);assert(reset_call()==FNV_VK_INVALID);assert(!release(def));assert(!reset_call());reset_checks+=2;
    uint32_t managed=make_buffer(0,16,0,1,0);uintptr_t native_buffer=buffer_ids[0];unsigned char bytes[16];for(unsigned i=0;i<16;++i)bytes[i]=(unsigned char)(i*11);
    upload_buffer(managed,0,16,0,bytes);uint32_t stream[]={device_at,0,managed,0,16};assert(!call(B_STREAM,5,stream));
    uint32_t texture=make_texture(2,2,1,0,21,1);uintptr_t native_texture=texture_id;unsigned pitch;unsigned char *mapped=lock_texture(texture,0,0,0,&pitch),expected[16];
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=mapped+y*pitch+x*4;memset(p,(int)(40+y*2+x),4);memset(expected+(y*2+x)*4,(int)(40+y*2+x),4);}
    assert(!unlock_texture(texture,0));uint32_t bind[]={device_at,0,texture};assert(!call(T_BIND,3,bind));
    unsigned stage_index[2]={0};for(unsigned i=0;i<catalog_count;++i)stage_index[((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff]=i;
    uint32_t shaders[2]={make_shader(stage_index[0]),make_shader(stage_index[1])};uint32_t shader_bind[]={device_at,shaders[0]};assert(!call(S_VS_BIND,2,shader_bind));shader_bind[1]=shaders[1];assert(!call(S_PS_BIND,2,shader_bind));
    uint32_t value[4]={0x3f800000,0x40000000,0x40400000,0x40800000};memcpy(ptr(DATA),value,16);uint32_t constants[]={device_at,7,DATA,1};assert(!call(VS_SF,4,constants));
    uint32_t state[]={device_at,22,1};assert(!call(SET_RENDER,3,state));uintptr_t old=native_color();
    uint64_t allocation=stats().allocated_bytes;
    for(unsigned fault=0;fault<3;++fault){if(fault==0)fail_Image=1;else if(fault==1)fail_ImageView=1;else fail_memory=1;
        assert(reset_call()==FNV_VK_OOM && native_color()==old && stats().allocated_bytes==allocation);assert(call(F_COOPERATIVE,1,&device_at)==0x88760869u);reset_checks+=2;}
    assert(!reset_call() && native_color()!=old && !call(F_COOPERATIVE,1,&device_at));reset_checks++;
    uint32_t get_args[]={device_at,GUEST_OUT};assert(!call(S_VS_GET,2,get_args) && !get(GUEST_OUT));assert(!call(S_PS_GET,2,get_args) && !get(GUEST_OUT));reset_checks+=2;
    uint32_t get_stream[]={device_at,0,GUEST_OUT,GUEST_OUT+4,GUEST_OUT+8};assert(!call(B_GET_STREAM,5,get_stream) && !get(GUEST_OUT) && !get(GUEST_OUT+4) && !get(GUEST_OUT+8));reset_checks++;
    uint32_t get_tex[]={device_at,0,GUEST_OUT};assert(!call(T_GET,3,get_tex) && !get(GUEST_OUT));reset_checks++;
    memset(ptr(DATA),0xa5,16);assert(!call(VS_GF,4,constants));for(unsigned i=0;i<16;++i)assert(ptr(DATA)[i]==0);reset_checks++;
    state[2]=GUEST_OUT;assert(!call(GET_RENDER,3,state) && get(GUEST_OUT)==3);reset_checks++;
    check_buffer(native_buffer,0,16,bytes);check_texture(native_texture,0,expected,16);reset_checks+=2;
    for(unsigned stage=0;stage<2;++stage){uint32_t function[]={shaders[stage],0,GUEST_OUT};assert(!call(GET_FUNCTION,3,function) && get(GUEST_OUT)==catalog[stage_index[stage]].size);assert(!release(shaders[stage]));reset_checks++;}
    assert(!release(managed) && !release(texture));assert(!stats().live_buffers && !stats().live_textures && !stats().live_shaders);
    HANDLE other=CreateThread(NULL,0,reset_other_thread,NULL,0,NULL);assert(other && WaitForSingleObject(other,10000)==WAIT_OBJECT_0);CloseHandle(other);assert(!call(F_COOPERATIVE,1,&device_at));reset_checks+=2;
    /* Public-zero MANAGED textures survive Reset when a block retains them. */
    uint32_t retained=make_texture(2,2,1,0,21,1),held_vs=make_shader(stage_index[0]),held_ps=make_shader(stage_index[1]);assert(!call(SB_BEGIN,1,&device_at));block_texture(0,retained);block_shader_set(0,held_vs);block_shader_set(1,held_ps);uint32_t block=end_block();assert(!release(held_vs) && !release(held_ps));assert(!release(retained));assert(!call(SB_APPLY,1,&block));
    assert(!reset_call());uint32_t current[]={device_at,0,GUEST_OUT};assert(!call(T_GET,3,current) && !get(GUEST_OUT));assert(!call(SB_APPLY,1,&block));assert(!call(T_GET,3,current) && get(GUEST_OUT)==retained);assert(!release(retained));block_shader_is(0,held_vs);block_shader_is(1,held_ps);unsigned char vs_data[64],ps_data[32];assert(!fnv_vk_backend_read_uniforms(gpu,vs_data,64,ps_data,32));block_shader_set(0,0);block_shader_set(1,0);block_texture(0,0);assert(!release(block) && !stats().live_textures && !stats().live_shaders);reset_checks+=4;
    /* A DEFAULT texture in a block still prevents Reset after public Release. */
    retained=make_texture(2,2,1,0,21,0);assert(!call(SB_BEGIN,1,&device_at));block_texture(0,retained);block=end_block();assert(!release(retained));assert(reset_call()==FNV_VK_INVALID);assert(fnv_vk_backend_reset(gpu,10000000000ull)==FNV_VK_INVALID);assert(!release(block) && !stats().live_textures);assert(!reset_call());reset_checks+=3;
}
static void exercise_reset_timeout(const struct fnv_vk_config *cfg){
    struct fnv_vk_backend *d;struct fnv_d3d9_shader_backend b;VkResult reason;assert(!fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason));fnv_vk_backend_callbacks(d,&b);
    assert(!b.clear(d,0,NULL,1,0xff123456,0,0));assert(!fnv_vk_backend_flush(d));timeout_fence=1;
    assert(fnv_vk_backend_reset(d,1)==FNV_VK_LOST && !timeout_fence && b.cooperative(d)==FNV_VK_LOST);b.device_release(d);assert(!fnv_vk_backend_destroy(d));no_live_handles();
}
