enum {S_VS_BIND=53,S_VS_GET,S_PS_BIND,S_PS_GET,S_DECL_CREATE,S_DECL_BIND,S_DECL_GET,S_DECL_ELEMENTS};
static uintptr_t declaration_id;
static uint32_t (*original_declaration_create)(void*,const void*,uint32_t,uintptr_t*);
static uint32_t capture_declaration(void *ctx,const void *elements,uint32_t count,uintptr_t *out){
    uint32_t hr=original_declaration_create(ctx,elements,count,out);if(!hr)declaration_id=*out;return hr;
}
static void load_shader_catalog(const char *path){
    FILE *f=fopen(path,"rb");assert(f);char magic[8];assert(fread(magic,1,8,f)==8 && !memcmp(magic,"FNV9SHD1",8));
    catalog_count=read_file32(f);assert(catalog_count==2);catalog=calloc(catalog_count,sizeof(*catalog));assert(catalog);
    for(unsigned i=0;i<catalog_count;++i){uint32_t stage=read_file32(f),size=read_file32(f);assert(stage<2 && size>=8 && size<=1048576);
        void *bytes=malloc(size);assert(bytes && fread(bytes,1,size,f)==size);catalog[i]=(struct fnv_d3d9_shader_code){bytes,size};}
    assert(fgetc(f)==EOF);fclose(f);
}
static void register_shader_files(const char *vs_path,const char *ps_path){
    struct fnv_uniform_entry vertex[4]={{0,7,1,0,16},{0,9,1,16,16},{1,2,1,32,16},{2,3,1,48,16}};
    struct fnv_uniform_entry pixel[2]={{0,3,1,0,16},{0,12,1,16,16}};
    for(unsigned i=0;i<catalog_count;++i){const struct fnv_d3d9_shader_code *code=catalog+i;
        unsigned stage=((const uint32_t*)code->bytes)[0]>>16==0xffff;
        FILE *f=fopen(stage?ps_path:vs_path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=20 && size<1048576);rewind(f);
        void *spirv=malloc((size_t)size);assert(spirv && fread(spirv,1,(size_t)size,f)==(size_t)size);fclose(f);
        struct fnv_uniform_plan plan={stage?32:64,stage?2:4,stage?pixel:vertex};
        unsigned char *copy=malloc(code->size);assert(copy);memcpy(copy,code->bytes,code->size);
        struct fnv_uniform_entry bad_entry={9,0,1,0,16};struct fnv_uniform_plan bad_plan={16,1,&bad_entry};
        assert(fnv_vk_backend_register_shader(gpu,stage,copy,code->size,spirv,(uint32_t)size,&bad_plan)==FNV_VK_INVALID);
        uint32_t saved=((uint32_t*)spirv)[0];((uint32_t*)spirv)[0]=0;
        assert(fnv_vk_backend_register_shader(gpu,stage,copy,code->size,spirv,(uint32_t)size,&plan)==FNV_VK_INVALID);((uint32_t*)spirv)[0]=saved;
        assert(!fnv_vk_backend_register_shader(gpu,stage,copy,code->size,spirv,(uint32_t)size,&plan));
        assert(fnv_vk_backend_register_shader(gpu,stage,copy,code->size,spirv,(uint32_t)size,&plan)==FNV_VK_INVALID);
        /* Stage/header failures never insert an entry. */
        assert(fnv_vk_backend_register_shader(gpu,1-stage,copy,code->size,spirv,(uint32_t)size,&plan)==FNV_VK_INVALID);
        memset(copy,0,code->size);memset(spirv,0,(size_t)size);free(copy);free(spirv);
    }
    memset(vertex,0,sizeof(vertex));memset(pixel,0,sizeof(pixel));assert(stats().shader_catalog_entries==2);
}
static void exercise_shaders(void){
    uint32_t ids[2]={0};
    for(unsigned i=0;i<catalog_count;++i){unsigned stage=((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff;
        memcpy(ptr(code_at),catalog[i].bytes,catalog[i].size);uint32_t create[]={device_at,code_at,GUEST_OUT};
        fail_ShaderModule=1;uint32_t before=stats().live_shaders;
        assert(call(stage?CREATE_PS:CREATE_VS,3,create)==FNV_VK_OOM && !get(GUEST_OUT) && stats().live_shaders==before && !fail_ShaderModule);
        ids[stage]=make_shader(i);uint32_t function[]={ids[stage],0,GUEST_OUT+4};assert(!call(GET_FUNCTION,3,function) && get(GUEST_OUT+4)==catalog[i].size);
        function[1]=DATA;assert(!call(GET_FUNCTION,3,function) && !memcmp(ptr(DATA),catalog[i].bytes,catalog[i].size));
        uint32_t bind[]={device_at,ids[stage]};assert(!call(stage?S_PS_BIND:S_VS_BIND,2,bind));
        assert(!release(ids[stage]));uint32_t get_args[]={device_at,GUEST_OUT};assert(!call(stage?S_PS_GET:S_VS_GET,2,get_args) && get(GUEST_OUT)==ids[stage]);assert(!release(ids[stage]));
    }
    assert(stats().live_shaders==2 && live_ShaderModule==2);
    /* Unregistered tokens and wrong-stage bindings leave handles/state untouched. */
    unsigned char unknown[8]={0,3,254,255,255,255,0,0};uintptr_t absent=0x1234;
    assert(provider.create(gpu,FNV_D3D9_VERTEX,unknown,8,&absent)==FNV_VK_INVALID && absent==0x1234);
    /* Upload reflected F/I/B banks through x86 setters and the real provider. */
    uint32_t values[6][4]={{0x3f800000,0x3e800000,0xbf000000,0x3f000000},{0x3f000000,0,0,0},
        {7,0xfffffffe,9,0},{0x80000000,0,0,0},{0x3e000000,0x3e800000,0x3f000000,0x3f800000},{0x3f800000,0,0,0}};
    const unsigned methods[6]={VS_SF,VS_SF,VS_SI,VS_SB,PS_SF,PS_SF},first[6]={7,9,2,3,3,12};
    for(unsigned i=0;i<6;++i){memcpy(ptr(DATA),values[i],16);uint32_t args[]={device_at,first[i],DATA,1};assert(!call(methods[i],4,args));memset(ptr(DATA),0,16);}
    assert(!fnv_d3d9_com_flush(com,provider.upload_constants,gpu));
    unsigned char vs[80],ps[48];memset(vs,0xa5,sizeof(vs));memset(ps,0xa5,sizeof(ps));
    assert(fnv_vk_backend_read_uniforms(gpu,vs,63,ps,32)==FNV_VK_INVALID);
    for(unsigned i=0;i<80;++i)assert(vs[i]==0xa5);for(unsigned i=0;i<48;++i)assert(ps[i]==0xa5);
    assert(!fnv_vk_backend_read_uniforms(gpu,vs,64,ps,32));
    unsigned char expected_vs[64]={0};for(unsigned i=0;i<3;++i)memcpy(expected_vs+i*16,values[i],16);uint32_t one=1;memcpy(expected_vs+48,&one,4);
    assert(!memcmp(vs,expected_vs,64) && !memcmp(ps,values[4],16) && !memcmp(ps+16,values[5],16));
    for(unsigned i=64;i<80;++i)assert(vs[i]==0xa5);for(unsigned i=32;i<48;++i)assert(ps[i]==0xa5);
    uint32_t unbind[]={device_at,0};assert(!call(S_VS_BIND,2,unbind));
    memset(vs,0xa5,sizeof(vs));assert(fnv_vk_backend_read_uniforms(gpu,vs,64,ps,32)==FNV_VK_INVALID);
    for(unsigned i=0;i<80;++i)assert(vs[i]==0xa5);
    assert(!call(S_PS_BIND,2,unbind));assert(!stats().live_shaders && !live_ShaderModule);
    /* UBYTE4/SHORT2/SHORT4 are float-valued scaled attributes, not integer shader inputs.
     * This checks format selection/caps, not GPU decoding of these formats. */
    const VkFormat scaled[3]={VK_FORMAT_R8G8B8A8_USCALED,VK_FORMAT_R16G16_SSCALED,VK_FORMAT_R16G16B16A16_SSCALED};
    for(unsigned type=5;type<=7;++type){unsigned char e[16]={0,0,0,0,(unsigned char)type,0,0,0,255,0,0,0,17,0,0,0};
        uintptr_t out=0x1234;uint32_t hr=provider.create_declaration(gpu,e,2,&out);assert(last_vertex_format==scaled[type-5]);
        if(last_vertex_features&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT){assert(!hr);provider.release(gpu,out);}else assert(hr==FNV_VK_INVALID && out==0x1234);}
    /* Declarations own bytes, semantic uniqueness and physical vertex formats. */
    const unsigned char elements[24]={0,0,0,0,1,0,0,0,0,0,8,0,1,0,5,0,255,0,0,0,17,0,0,0};
    memcpy(ptr(DATA),elements,24);uint32_t create[]={device_at,DATA,GUEST_OUT};assert(!call(S_DECL_CREATE,3,create));uint32_t decl=get(GUEST_OUT);uintptr_t native=declaration_id;
    memset(ptr(DATA),0,24);unsigned char owned[32];memset(owned,0xa5,32);assert(!fnv_vk_backend_read_declaration(gpu,native,owned,24) && !memcmp(owned,elements,24));
    for(unsigned i=24;i<32;++i)assert(owned[i]==0xa5);
    uint32_t get_elements[]={decl,DATA,GUEST_OUT+4};put(GUEST_OUT+4,3);assert(!call(S_DECL_ELEMENTS,3,get_elements) && !memcmp(ptr(DATA),elements,24));
    unsigned char invalid[24];memcpy(invalid,elements,24);invalid[14]=0;uintptr_t out=0x1234;
    assert(provider.create_declaration(gpu,invalid,3,&out)==FNV_VK_INVALID && out==0x1234);
    memcpy(invalid,elements,24);invalid[4]=13;assert(provider.create_declaration(gpu,invalid,3,&out)==FNV_VK_INVALID);
    uint32_t bind[]={device_at,decl};assert(!call(S_DECL_BIND,2,bind));assert(!release(decl));uint32_t get_decl[]={device_at,GUEST_OUT};
    assert(!call(S_DECL_GET,2,get_decl) && get(GUEST_OUT)==decl);assert(!release(decl));
    bind[1]=0;assert(!call(S_DECL_BIND,2,bind));assert(!stats().live_declarations);
    memset(owned,0xa5,32);assert(fnv_vk_backend_read_declaration(gpu,native,owned,32)==FNV_VK_INVALID);
    for(unsigned i=0;i<32;++i)assert(owned[i]==0xa5);
}

static void leave_shader_bound_for_close(void){
    uint32_t s=make_shader(0);unsigned stage=((const uint32_t*)catalog[0].bytes)[0]>>16==0xffff;
    uint32_t bind[]={device_at,s};assert(!call(stage?S_PS_BIND:S_VS_BIND,2,bind));assert(!release(s));
    const unsigned char e[16]={0,0,0,0,1,0,0,0,255,0,0,0,17,0,0,0};memcpy(ptr(DATA),e,16);
    uint32_t create[]={device_at,DATA,GUEST_OUT};assert(!call(S_DECL_CREATE,3,create));uint32_t declaration=get(GUEST_OUT);
    bind[1]=declaration;assert(!call(S_DECL_BIND,2,bind));assert(!release(declaration));
    assert(stats().live_shaders==1 && stats().live_declarations==1);
}
