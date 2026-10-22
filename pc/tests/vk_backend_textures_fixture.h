enum {T_CREATE=46,T_LOCK,T_UNLOCK,T_BIND,T_GET,T_LEVELS,T_DESC};
static uint32_t (*original_texture_create)(void*,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uintptr_t*);
static uintptr_t texture_id;
static unsigned texture_bytes_checked,texture_upload_faults;
static uint32_t capture_texture(void *ctx,uint32_t w,uint32_t h,uint32_t levels,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    uint32_t hr=original_texture_create(ctx,w,h,levels,usage,format,pool,out);if(!hr)texture_id=*out;return hr;
}
static struct {uint32_t address,w,h,format;uintptr_t id;} texture_lookup[128];
static unsigned texture_lookup_count;
static uint32_t make_texture(unsigned w,unsigned h,unsigned levels,unsigned usage,unsigned format,unsigned pool){
    uint32_t args[]={device_at,w,h,levels,usage,format,pool,GUEST_OUT,0};assert(!call(T_CREATE,9,args));uint32_t address=get(GUEST_OUT);unsigned i=0;while(i<texture_lookup_count && texture_lookup[i].address!=address)i++;assert(i<128);if(i==texture_lookup_count)texture_lookup_count++;
    texture_lookup[i].address=address;texture_lookup[i].w=w;texture_lookup[i].h=h;texture_lookup[i].format=format;texture_lookup[i].id=texture_id;return address;
}
static void texture_ready(uint32_t address){
    unsigned i=0;while(i<texture_lookup_count && texture_lookup[i].address!=address)i++;assert(i<texture_lookup_count);
    uint32_t w=texture_lookup[i].w,h=texture_lookup[i].h,f=texture_lookup[i].format;
    size_t bytes=f==0x31545844u || f==0x33545844u || f==0x35545844u?(size_t)((w+3)/4)*((h+3)/4)*(f==0x31545844u?8:16):(size_t)w*h*4;
    unsigned char *data=malloc(bytes);assert(data);assert(!fnv_vk_backend_read_texture(gpu,texture_lookup[i].id,0,data,bytes,10000000000ull));free(data);
}
static unsigned char *lock_texture(uint32_t resource,unsigned level,unsigned rect,unsigned flags,unsigned *pitch){
    uint32_t args[]={resource,level,GUEST_OUT,rect,flags};assert(!call(T_LOCK,5,args));*pitch=get(GUEST_OUT);return ptr(get(GUEST_OUT+4));
}
static uint32_t unlock_texture(uint32_t resource,unsigned level){uint32_t args[]={resource,level};return call(T_UNLOCK,2,args);}
static void check_texture(uintptr_t id,unsigned level,const unsigned char *expected,unsigned bytes){
    unsigned char data[272];assert(bytes<=256);memset(data,0xa5,sizeof(data));
    assert(!fnv_vk_backend_read_texture(gpu,id,level,data,bytes,10000000000ull));assert(!memcmp(data,expected,bytes));
    for(unsigned i=bytes;i<sizeof(data);++i)assert(data[i]==0xa5);texture_bytes_checked+=bytes;
}
static void exercise_textures(void){
    /* All supported conversions go through translated LockRect/UnlockRect and GPU copies. */
    const unsigned formats[5]={21,22,23,28,50};
    for(unsigned format=0;format<5;++format){
        uint32_t t=make_texture(3,2,1,0,formats[format],1);uintptr_t id=texture_id;unsigned pitch;
        unsigned char guard[24];memset(guard,0xa5,sizeof(guard));
        assert(fnv_vk_backend_read_texture(gpu,id,0,guard,sizeof(guard),0)==FNV_VK_INVALID);
        for(unsigned i=0;i<24;++i)assert(guard[i]==0xa5);
        unsigned char *dst=lock_texture(t,0,0,0,&pitch),expected[24];
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x){unsigned pixel=y*3+x;unsigned char *e=expected+pixel*4;
            if(format<2){unsigned char b=11+pixel,g=37+pixel,r=89+pixel,a=101+pixel;unsigned char *p=dst+y*pitch+x*4;
                p[0]=b;p[1]=g;p[2]=r;p[3]=a;e[0]=r;e[1]=g;e[2]=b;e[3]=format==0?a:255;}
            else if(format==2){const uint16_t values[6]={0xf800,0x07e0,0x001f,0xffff,0,0xf81f};uint16_t v=values[pixel];memcpy(dst+y*pitch+x*2,&v,2);
                e[0]=(v&0xf800)?255:0;e[1]=(v&0x07e0)?255:0;e[2]=(v&31)?255:0;e[3]=255;}
            else {unsigned char v=53+pixel*7;dst[y*pitch+x]=v;e[0]=e[1]=e[2]=format==3?0:v;e[3]=format==3?v:255;}}
        assert(!unlock_texture(t,0));check_texture(id,0,expected,24);
        uint32_t bind[]={device_at,3,t};assert(!call(T_BIND,3,bind));assert(!release(t));
        uint32_t get_args[]={device_at,3,GUEST_OUT};assert(!call(T_GET,3,get_args) && get(GUEST_OUT)==t);assert(!release(t));
        bind[2]=0;assert(!call(T_BIND,3,bind));assert(!stats().live_textures && !stats().texture_versions);
    }
    uint32_t t=make_texture(4,4,0,512,21,0);uintptr_t id=texture_id;unsigned pitch;
    uintptr_t rejected=0x1234;struct fnv_vk_stats before=stats();
    assert(provider.create_texture(gpu,4,4,1,0,0x32545844,1,&rejected)==FNV_VK_INVALID && rejected==0x1234);
    /* Render targets now support A8R8G8B8, but not X8R8G8B8. */
    assert(provider.create_texture(gpu,4,4,1,1,22,0,&rejected)==FNV_VK_INVALID);
    assert(provider.create_texture(gpu,4,4,1,0,21,2,&rejected)==FNV_VK_INVALID);
    /* Failed GPU-only target allocation must not publish a handle or leak memory. */
    for(unsigned fault=0;fault<3;++fault){
        if(fault==0)fail_memory=1;else if(fault==1)fail_Image=1;else fail_ImageView=1;
        assert(provider.create_texture(gpu,4,4,1,1,21,0,&rejected)==FNV_VK_OOM && rejected==0x1234);
        assert(stats().allocated_bytes==before.allocated_bytes && stats().texture_versions==before.texture_versions && stats().live_textures==before.live_textures);
    }
    unsigned char zero[64]={0};
    assert(provider.upload_texture(gpu,id,0,1,0,3,4,0x2000,16,zero)==FNV_VK_INVALID);
    assert(provider.upload_texture(gpu,id,0,0,0,4,4,0,4,zero)==FNV_VK_INVALID);
    assert(provider.bind_texture(gpu,16,id)==FNV_VK_INVALID);
    assert(stats().allocated_bytes==before.allocated_bytes && stats().texture_versions==before.texture_versions);
    assert(call(T_LEVELS,1,&t)==3);uint32_t desc[]={t,2,DATA};assert(!call(T_DESC,3,desc) && get(DATA+24)==1 && get(DATA+28)==1);
    unsigned char expected[3][64];memset(expected,0,sizeof(expected));
    for(unsigned level=0;level<3;++level){unsigned size=4>>level;unsigned char *dst=lock_texture(t,level,0,0,&pitch);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){unsigned char *p=dst+y*pitch+x*4,*e=expected[level]+(y*size+x)*4;
            p[0]=10+level;p[1]=20+x;p[2]=30+y;p[3]=200;e[0]=p[2];e[1]=p[1];e[2]=p[0];e[3]=p[3];}
        assert(!unlock_texture(t,level));check_texture(id,level,expected[level],size*size*4);}
    /* Partial update errors preserve image/shadow and leave UnlockRect retryable. */
    const uint32_t rect[4]={1,1,3,3};memcpy(ptr(DATA),rect,16);unsigned char *dst=lock_texture(t,0,DATA,0,&pitch);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){unsigned char *p=dst+y*pitch+x*4;p[0]=7;p[1]=17;p[2]=27;p[3]=37;}
    assert(!unlock_texture(t,0));
    for(unsigned fault=0;fault<5;++fault){
        if(fault==0)fail_memory=1;else if(fault==1)fail_Image=1;else if(fault==2)fail_ImageView=1;else if(fault==3)fail_map=1;else fail_flush=1;
        unsigned char untouched[64];memset(untouched,0xa5,64);
        assert(fnv_vk_backend_read_texture(gpu,id,0,untouched,64,10000000000ull)==FNV_VK_OOM);texture_upload_faults++;
        for(unsigned i=0;i<64;++i)assert(untouched[i]==0xa5);
    }
    for(unsigned y=1;y<3;++y)for(unsigned x=1;x<3;++x){unsigned char *e=expected[0]+(y*4+x)*4;e[0]=27;e[1]=17;e[2]=7;e[3]=37;}
    for(unsigned level=0;level<3;++level)check_texture(id,level,expected[level],(4>>level)*(4>>level)*4);
    /* All mip Unlocks only patch CPU shadow, without consuming command slots. */
    uint64_t before_mips=stats().submissions;
    for(unsigned level=0;level<3;++level){dst=lock_texture(t,level,0,0,&pitch);assert(!unlock_texture(t,level));}
    assert(stats().submissions==before_mips && !stats().pending_frames);
    for(unsigned level=0;level<3;++level)check_texture(id,level,expected[level],(4>>level)*(4>>level)*4);
    /* Old image is kept through its timed-out readback while DISCARD submits a new image. */
    unsigned char untouched[64];memset(untouched,0xa5,sizeof(untouched));timeout_fence=1;
    assert(fnv_vk_backend_read_texture(gpu,id,0,untouched,sizeof(untouched),0)==FNV_VK_BUSY);
    dst=lock_texture(t,0,0,0x2000,&pitch);for(unsigned y=0;y<4;++y)memset(dst+y*pitch,0x33,16);
    assert(!unlock_texture(t,0));assert(stats().pending_frames==1 && stats().texture_versions==1);
    memset(expected,0,sizeof(expected));memset(expected[0],0x33,64);
    for(unsigned i=0;i<64;++i)assert(untouched[i]==0xa5);
    for(unsigned level=0;level<3;++level)check_texture(id,level,expected[level],(4>>level)*(4>>level)*4);
    assert(stats().texture_versions==1);
    /* READONLY lock must not upload a version. */
    uint64_t sent=stats().submissions;dst=lock_texture(t,0,0,0x10,&pitch);assert(dst && !unlock_texture(t,0));assert(stats().submissions==sent);
    /* A transfer, not a public reference, keeps the texture alive. */
    timeout_fence=1;assert(fnv_vk_backend_read_texture(gpu,id,0,untouched,sizeof(untouched),0)==FNV_VK_BUSY);
    assert(!release(t) && stats().live_textures==1);assert(!fnv_vk_backend_wait(gpu,10000000000ull));
    assert(!stats().live_textures && !stats().texture_versions);
    assert(fnv_vk_backend_read_texture(gpu,id,0,untouched,sizeof(untouched),0)==FNV_VK_INVALID);
    t=make_texture(2,2,1,0,21,1);dst=lock_texture(t,0,0,0,&pitch);memset(dst,0x55,16);
    assert(!unlock_texture(t,0));assert(!release(t) && !stats().live_textures && !stats().pending_frames);
    assert(!fnv_vk_backend_wait(gpu,10000000000ull));assert(!stats().live_textures && !stats().texture_versions);
}

static void exercise_texture_submit_faults(const struct fnv_vk_config *cfg){
    for(unsigned fault=0;fault<3;++fault){
        struct fnv_vk_backend *d;struct fnv_d3d9_shader_backend b;VkResult reason;uintptr_t id;
        assert(!fnv_vk_backend_create(proxy_gpa,cfg,&d,&reason));fnv_vk_backend_callbacks(d,&b);
        assert(!b.create_texture(d,2,2,1,0,21,1,&id));const unsigned char source[16]={0};
        assert(!b.upload_texture(d,id,0,0,0,2,2,0,8,source));assert(!fnv_vk_backend_wait(d,10000000000ull));
        if(fault==0)fail_end=1;else if(fault==1)fail_reset=1;else fail_submit=1;
        assert(!b.upload_texture(d,id,0,0,0,2,2,0,8,source));unsigned char out[16];memset(out,0xa5,16);assert(fnv_vk_backend_read_texture(d,id,0,out,16,10000000000ull)==(fault==2?FNV_VK_LOST:FNV_VK_OOM));for(unsigned i=0;i<16;++i)assert(out[i]==0xa5);
        assert(fnv_vk_backend_poll(d)==FNV_VK_LOST);
        b.release(d,id);b.device_release(d);assert(!fnv_vk_backend_destroy(d));no_live_handles();
    }
}
