enum {B_VB=37,B_IB,B_LOCK,B_UNLOCK,B_STREAM,B_INDICES,B_FREQ,B_GET_STREAM,B_GET_INDICES};
static uint32_t (*original_buffer_create)(void*,enum fnv_d3d9_resource_kind,uint32_t,uint32_t,uint32_t,uint32_t,uintptr_t*);
static uintptr_t buffer_ids[2];
static unsigned buffer_bytes_checked;
static uint32_t capture_buffer(void *ctx,enum fnv_d3d9_resource_kind kind,uint32_t bytes,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    uint32_t hr=original_buffer_create(ctx,kind,bytes,usage,format,pool,out);if(!hr)buffer_ids[kind==FNV_D3D9_IB]=*out;return hr;
}
static uint32_t make_buffer(int index,uint32_t bytes,uint32_t usage,uint32_t pool,uint32_t format){
    uint32_t args[]={device_at,bytes,usage,index?format:0,pool,GUEST_OUT,0};assert(!call(index?B_IB:B_VB,7,args));return get(GUEST_OUT);
}
static uint32_t map_buffer(uint32_t resource,unsigned offset,unsigned bytes,unsigned flags){
    uint32_t args[]={resource,offset,bytes,GUEST_OUT,flags};assert(!call(B_LOCK,5,args));return get(GUEST_OUT);
}
static void upload_buffer(uint32_t resource,unsigned offset,unsigned bytes,unsigned flags,const void *data){
    memcpy(ptr(map_buffer(resource,offset,bytes,flags)),data,bytes);assert(!call(B_UNLOCK,1,&resource));
}
static void check_buffer(uintptr_t id,unsigned offset,unsigned bytes,const void *expected){
    unsigned char readback[272];assert(bytes<=256);memset(readback,0xa5,sizeof(readback));
    assert(!fnv_vk_backend_read_buffer(gpu,id,offset,bytes,readback,bytes,10000000000ull));assert(!memcmp(readback,expected,bytes));
    for(unsigned i=bytes;i<sizeof(readback);++i)assert(readback[i]==0xa5);
    buffer_bytes_checked+=bytes;
}
static void exercise_buffers(void){
    uint32_t vb=make_buffer(0,128,512,0,0);uintptr_t native_vb=buffer_ids[0];
    uint32_t ib=make_buffer(1,12,0,1,101);uintptr_t native_ib=buffer_ids[1];
    uint32_t stream[]={device_at,0,vb,0,16},index[]={device_at,ib};assert(!call(B_STREAM,5,stream) && !call(B_INDICES,2,index));
    uint32_t frequency_args[]={device_at,0,1};assert(!call(B_FREQ,3,frequency_args));frequency_args[2]=0x40000002;
    assert(call(B_FREQ,3,frequency_args)==FNV_VK_INVALID);
    uint32_t wrong[]={device_at,0,ib,0,16};assert(call(B_STREAM,5,wrong)==FNV_VK_INVALID);
    uint32_t get_stream[]={device_at,0,GUEST_OUT,GUEST_OUT+4,GUEST_OUT+8};assert(!call(B_GET_STREAM,5,get_stream) && get(GUEST_OUT)==vb && get(GUEST_OUT+8)==16);assert(release(vb)==1);
    uint32_t get_index[]={device_at,GUEST_OUT};assert(!call(B_GET_INDICES,2,get_index) && get(GUEST_OUT)==ib);assert(release(ib)==1);
    unsigned char expected[128];for(unsigned i=0;i<128;++i)expected[i]=(unsigned char)(i^0x5a);
    const uint16_t indices[6]={0,1,2,0,2,3};upload_buffer(vb,0,128,0,expected);upload_buffer(ib,0,12,0,indices);
    check_buffer(native_vb,0,128,expected);check_buffer(native_ib,0,12,indices);
    memset(ptr(map_buffer(vb,3,17,0)),0xde,17);
    for(unsigned failure=0;failure<3;++failure){
        if(failure==0)fail_memory=1;else if(failure==1)fail_map=1;else fail_flush=1;
        assert(call(B_UNLOCK,1,&vb)==FNV_VK_OOM);check_buffer(native_vb,0,128,expected);
    }
    assert(!fail_memory && !fail_map && !fail_flush);assert(!call(B_UNLOCK,1,&vb));memset(expected+3,0xde,17);
    check_buffer(native_vb,0,128,expected);check_buffer(native_vb,3,17,expected+3);
    unsigned char untouched[8];memset(untouched,0xa5,sizeof(untouched));timeout_fence=1;
    assert(fnv_vk_backend_read_buffer(gpu,native_vb,1,7,untouched,7,0)==FNV_VK_BUSY);
    assert(stats().pending_frames==1 && stats().buffer_versions==2);
    memset(ptr(map_buffer(vb,4,5,0x2000)),0x33,5);assert(!call(B_UNLOCK,1,&vb));
    memset(expected,0,128);memset(expected+4,0x33,5);
    assert(stats().pending_frames==1 && stats().buffer_versions==3); /* old GPU version retained */
    for(unsigned i=0;i<8;++i)assert(untouched[i]==0xa5);
    check_buffer(native_vb,0,128,expected);assert(stats().buffer_versions==2);
    assert(!release(vb)); /* binding can resurrect its public interface */
    assert(!call(B_GET_STREAM,5,get_stream) && get(GUEST_OUT)==vb);assert(!release(vb));
    timeout_fence=1;assert(fnv_vk_backend_read_buffer(gpu,native_vb,0,128,expected,sizeof(expected),0)==FNV_VK_BUSY);
    stream[2]=stream[3]=stream[4]=0;index[1]=0;assert(!call(B_STREAM,5,stream) && !call(B_INDICES,2,index));assert(!release(ib));
    assert(stats().live_buffers==1 && stats().buffer_versions==1); /* only in-flight VB */
    assert(!fnv_vk_backend_wait(gpu,10000000000ull));assert(!stats().live_buffers && !stats().buffer_versions);
    assert(fnv_vk_backend_read_buffer(gpu,native_vb,0,8,untouched,8,0)==FNV_VK_INVALID);
    for(unsigned i=0;i<8;++i)assert(untouched[i]==0xa5);
    uint32_t ib32=make_buffer(1,16,0,1,102);uint32_t indices32[4]={0,0x1ffff,2,3};upload_buffer(ib32,0,16,0,indices32);
    check_buffer(buffer_ids[1],0,16,indices32);assert(!release(ib32));
    for(unsigned n=1;n<=8;++n){uint32_t small=make_buffer(0,n,0,1,0);unsigned char data[8];memset(data,(int)(0x20+n),sizeof(data));
        upload_buffer(small,0,n,0,data);check_buffer(buffer_ids[0],n-1,1,data+n-1);assert(!release(small));}
    assert(!stats().live_buffers && !stats().buffer_versions && flushes);
}
