/* Own COM geometry integration fixture. NULLREF is only the resource/lifetime
 * oracle. Owned byte copies reach Vulkan; it never reads guest/oracle storage. */
#include "d3d9_geometry.h"
enum {G_CREATE_VB=24,G_CREATE_IB,G_LOCK,G_UNLOCK,G_STREAM,G_INDICES,G_CREATE_DECL,G_DECL};
static unsigned char geometry_bytes[2][80],geometry_saved[3][2][80];
static uintptr_t geometry_handles[2],geometry_decl;
static uint32_t geometry_resources[3];
static struct fnv_geometry_input geometry;
static unsigned geometry_recorded,geometry_updates;
static unsigned char geometry_elements[24];
static uint32_t geometry_create(void *ctx,enum fnv_d3d9_resource_kind kind,uint32_t bytes,uint32_t usage,uint32_t format,uint32_t pool,uintptr_t *out){
    (void)ctx;unsigned index=kind==FNV_D3D9_IB;
    if((kind!=FNV_D3D9_VB && kind!=FNV_D3D9_IB) || bytes!=(index?16:80) || geometry_handles[index])return FNV_D3D9_INVALIDCALL;
    HRESULT hr=index?IDirect3DDevice9_CreateIndexBuffer(device,bytes,usage,format,pool,(IDirect3DIndexBuffer9**)out,NULL):
        IDirect3DDevice9_CreateVertexBuffer(device,bytes,usage,format,pool,(IDirect3DVertexBuffer9**)out,NULL);
    if(SUCCEEDED(hr))geometry_handles[index]=*out;
    return (uint32_t)hr;
}
static uint32_t geometry_update(void *ctx,uintptr_t handle,uint32_t offset,uint32_t bytes,uint32_t flags,const void *data){
    (void)ctx;unsigned index=handle==geometry_handles[1];
    if(handle!=geometry_handles[index] || offset>(index?16u:80u) || bytes>(index?16u:80u)-offset)return FNV_D3D9_INVALIDCALL;
    void *mapped;HRESULT hr=index?IDirect3DIndexBuffer9_Lock((IDirect3DIndexBuffer9*)handle,offset,bytes,&mapped,flags):
        IDirect3DVertexBuffer9_Lock((IDirect3DVertexBuffer9*)handle,offset,bytes,&mapped,flags);
    if(FAILED(hr))return (uint32_t)hr;
    memcpy(mapped,data,bytes);
    hr=index?IDirect3DIndexBuffer9_Unlock((IDirect3DIndexBuffer9*)handle):IDirect3DVertexBuffer9_Unlock((IDirect3DVertexBuffer9*)handle);
    if(SUCCEEDED(hr)){memcpy(geometry_bytes[index]+offset,data,bytes);geometry_updates++;}
    return (uint32_t)hr;
}
static uint32_t geometry_stream(void *ctx,uint32_t stream,uintptr_t handle,uint32_t offset,uint32_t stride){
    (void)ctx;if(stream || (handle && handle!=geometry_handles[0]))return FNV_D3D9_INVALIDCALL;
    HRESULT hr=IDirect3DDevice9_SetStreamSource(device,stream,(IDirect3DVertexBuffer9*)handle,offset,stride);
    if(SUCCEEDED(hr))geometry.streams[0]=(struct fnv_geometry_stream){handle?80:0,offset,stride,1};
    return (uint32_t)hr;
}
static uint32_t geometry_indices(void *ctx,uintptr_t handle){
    (void)ctx;if(handle && handle!=geometry_handles[1])return FNV_D3D9_INVALIDCALL;
    HRESULT hr=IDirect3DDevice9_SetIndices(device,(IDirect3DIndexBuffer9*)handle);
    if(SUCCEEDED(hr)){geometry.indices=handle?geometry_bytes[1]:NULL;geometry.index_bytes=16;geometry.index_size=2;}
    return (uint32_t)hr;
}
static uint32_t geometry_create_decl(void *ctx,const void *elements,uint32_t count,uintptr_t *out){
    (void)ctx;if(count!=3 || geometry_decl)return FNV_D3D9_INVALIDCALL;
    HRESULT hr=IDirect3DDevice9_CreateVertexDeclaration(device,elements,(IDirect3DVertexDeclaration9**)out);
    if(SUCCEEDED(hr)){geometry_decl=*out;memcpy(geometry_elements,elements,24);}
    return (uint32_t)hr;
}
static uint32_t geometry_bind_decl(void *ctx,uintptr_t handle){
    (void)ctx;if(handle && handle!=geometry_decl)return FNV_D3D9_INVALIDCALL;
    HRESULT hr=IDirect3DDevice9_SetVertexDeclaration(device,(IDirect3DVertexDeclaration9*)handle);
    if(SUCCEEDED(hr)){geometry.elements=handle?geometry_elements:NULL;geometry.element_count=handle?3:0;}
    return (uint32_t)hr;
}
static void geometry_upload_bytes(uint32_t resource,const void *data,unsigned bytes){
    uint32_t args[]={resource,0,bytes,GUEST_OUT,0};assert(!call(G_LOCK,5,args));
    memcpy(ptr(get(GUEST_OUT)),data,bytes);assert(!call(G_UNLOCK,1,&resource));
}
static void geometry_frame(unsigned frame){
    const float vertices[16]={-1,1,0,0, 1,1,1,0, 1,-1,1,1, -1,-1,0,1};
    const uint16_t indices[8]={0,0,1,2,3,1,3,4};
    unsigned char vb[80]={0};uint16_t ib[8];memcpy(ib,indices,16);
    unsigned shift=frame%4;
    for(unsigned i=0;i<4;++i)memcpy(vb+16+i*16,vertices+((i+shift)%4)*4,16);
    for(unsigned i=2;i<8;++i)ib[i]=(uint16_t)((ib[i]-1+4-shift)%4+1);
    geometry_upload_bytes(geometry_resources[0],vb,80);geometry_upload_bytes(geometry_resources[1],ib,16);
}
static void geometry_setup(void){
    for(unsigned index=0;index<2;++index){
        uint32_t args[]={device_at,index?16:80,0,index?D3DFMT_INDEX16:0,D3DPOOL_MANAGED,GUEST_OUT,0};
        assert(!call(index?G_CREATE_IB:G_CREATE_VB,7,args));geometry_resources[index]=get(GUEST_OUT);
    }
    D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    memcpy(ptr(DATA),decl,sizeof(decl));uint32_t args[]={device_at,DATA,GUEST_OUT};
    assert(!call(G_CREATE_DECL,3,args));geometry_resources[2]=get(GUEST_OUT);
    args[1]=geometry_resources[2];assert(!call(G_DECL,2,args));
    uint32_t stream[]={device_at,0,geometry_resources[0],16,16};assert(!call(G_STREAM,5,stream));
    args[1]=geometry_resources[1];assert(!call(G_INDICES,2,args));
}
static uint32_t geometry_record(int indexed,uint32_t primitive,uint32_t base,uint32_t minv,uint32_t vertices,uint32_t start,uint32_t primitives){
    geometry.indexed=(uint32_t)indexed;geometry.primitive=primitive;geometry.base_bits=base;
    geometry.min_vertex=minv;geometry.vertices=vertices;geometry.start=start;geometry.primitives=primitives;
    struct fnv_geometry_access access;uint32_t hr=fnv_d3d9_geometry_validate(&geometry,&access);if(hr)return hr;
    assert(geometry_recorded<3 && access.count==6 && access.stream_mask==1 && !access.first_vertex && access.last_vertex==3);
    assert(access.streams[0].offset==16 && access.streams[0].bytes==64 && access.indices.offset==4 && access.indices.bytes==12);
    memcpy(geometry_saved[geometry_recorded][0],geometry_bytes[0]+16,64);
    memcpy(geometry_saved[geometry_recorded][1],geometry_bytes[1]+4,12);geometry_recorded++;return 0;
}
static void geometry_poison(void){
    unsigned char zero[80]={0};geometry_upload_bytes(geometry_resources[0],zero,80);geometry_upload_bytes(geometry_resources[1],zero,16);
    assert(geometry_recorded==3 && memcmp(geometry_saved[0][0],geometry_saved[1][0],64));
}
static void geometry_cleanup(void){
    uint32_t stream[]={device_at,0,0,0,0},args[]={device_at,0};assert(!call(G_STREAM,5,stream));
    assert(!call(G_INDICES,2,args) && !call(G_DECL,2,args));
    for(unsigned i=0;i<3;++i)assert(!release(geometry_resources[i]));
}
