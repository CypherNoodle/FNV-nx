#include "d3d9_constants.h"
static HRESULT native_constants(IDirect3DDevice9 *d,unsigned s,unsigned k,unsigned first,unsigned count,void *data,int get){
    if(!s){if(!k)return get?IDirect3DDevice9_GetVertexShaderConstantF(d,first,data,count):IDirect3DDevice9_SetVertexShaderConstantF(d,first,data,count);
        if(k==1)return get?IDirect3DDevice9_GetVertexShaderConstantI(d,first,data,count):IDirect3DDevice9_SetVertexShaderConstantI(d,first,data,count);
        return get?IDirect3DDevice9_GetVertexShaderConstantB(d,first,data,count):IDirect3DDevice9_SetVertexShaderConstantB(d,first,data,count);}
    if(!k)return get?IDirect3DDevice9_GetPixelShaderConstantF(d,first,data,count):IDirect3DDevice9_SetPixelShaderConstantF(d,first,data,count);
    if(k==1)return get?IDirect3DDevice9_GetPixelShaderConstantI(d,first,data,count):IDirect3DDevice9_SetPixelShaderConstantI(d,first,data,count);
    return get?IDirect3DDevice9_GetPixelShaderConstantB(d,first,data,count):IDirect3DDevice9_SetPixelShaderConstantB(d,first,data,count);
}
static uint32_t snapshot_random=0x1f352e79;
static uint32_t snapshot_rng(void){uint32_t x=snapshot_random;x^=x<<13;x^=x>>17;x^=x<<5;return snapshot_random=x;}
static unsigned snapshot_checks,snapshot_uploads,snapshot_fail;
static int snapshot_upload(void *ctx,enum fnv_d3d9_stage s,enum fnv_d3d9_constant_kind k,uint32_t first,uint32_t count,const uint32_t *bits){
    (void)ctx;(void)s;(void)k;(void)first;(void)count;(void)bits;snapshot_uploads++;if(snapshot_fail){snapshot_fail=0;return 0;}return 1;
}
static void constant_compare(IDirect3DDevice9 *d,struct fnv_d3d9_constants *c){
    for(unsigned s=0;s<2;++s)for(unsigned k=0;k<3;++k){unsigned limit=k?16:s?224:256,width=k==2?1:4;uint32_t actual[1024],expected[1024];
        assert(!fnv_d3d9_constants_get(c,s,k,0,limit,actual));ok(native_constants(d,s,k,0,limit,expected,1));assert(!memcmp(actual,expected,limit*width*4));snapshot_checks++;}
}
static void constant_fill(IDirect3DDevice9 *d,struct fnv_d3d9_constants *c,unsigned variant){
    for(unsigned s=0;s<2;++s)for(unsigned k=0;k<3;++k){unsigned limit=k?16:s?224:256,width=k==2?1:4;uint32_t data[1024];
        for(unsigned i=0;i<limit*width;++i){if(k==2)data[i]=(i+variant)&1;else if(k==1)data[i]=variant?snapshot_rng():0;else {float f=variant?(float)((int)(snapshot_rng()%512)-256)/16:0;memcpy(data+i,&f,4);}}
        assert(!fnv_d3d9_constants_set(c,s,k,0,limit,data));ok(native_constants(d,s,k,0,limit,data,0));}
}
static void constant_blocks(IDirect3DDevice9 *d){
    struct fnv_d3d9_constants *c;assert(!fnv_d3d9_constants_create(256,224,&c));
    for(unsigned round=0;round<64;++round){constant_fill(d,c,0);assert(!fnv_d3d9_constants_flush(c,snapshot_upload,NULL));struct fnv_d3d9_constant_snapshot snapshot={0};ok(IDirect3DDevice9_BeginStateBlock(d));
        for(unsigned i=0;i<32;++i){unsigned s=i%2,k=(i/2)%3,limit=k?16:s?224:256,count=1+snapshot_rng()%3,first=snapshot_rng()%(limit-count+1),width=k==2?1:4;uint32_t data[12];
            if(i==24){s=0;k=0;width=4;first=63;count=3;}if(i==30){s=0;k=0;width=4;first=64;count=1;}
            for(unsigned j=0;j<count*width;++j){if(k==2)data[j]=snapshot_rng()&1;else if(k==1)data[j]=snapshot_rng();else {float f=(float)((int)(snapshot_rng()%512)-256)/8;memcpy(data+j,&f,4);}}
            assert(!fnv_d3d9_constants_record(c,&snapshot,s,k,first,count,data));ok(native_constants(d,s,k,first,count,data,0));}
        constant_compare(d,c);IDirect3DStateBlock9 *block=NULL;ok(IDirect3DDevice9_EndStateBlock(d,&block));ok(IDirect3DStateBlock9_Apply(block));assert(!fnv_d3d9_constants_snapshot(c,&snapshot,0));constant_compare(d,c);
        snapshot_uploads=0;snapshot_fail=1;assert(fnv_d3d9_constants_flush(c,snapshot_upload,NULL)==FNV_D3D9_UPLOAD_FAILED && snapshot_uploads==1);assert(!fnv_d3d9_constants_flush(c,snapshot_upload,NULL) && snapshot_uploads>1);
        snapshot_uploads=0;assert(!fnv_d3d9_constants_flush(c,snapshot_upload,NULL) && !snapshot_uploads);
        constant_fill(d,c,1);ok(IDirect3DStateBlock9_Capture(block));assert(!fnv_d3d9_constants_snapshot(c,&snapshot,1));constant_fill(d,c,2);
        ok(IDirect3DStateBlock9_Apply(block));assert(!fnv_d3d9_constants_snapshot(c,&snapshot,0));constant_compare(d,c);IDirect3DStateBlock9_Release(block);
    }
    struct fnv_d3d9_constant_snapshot bad={0},saved;uint32_t values[4]={1,2,3,4},before[4],after[4];
    assert(!fnv_d3d9_constants_record(c,&bad,0,0,7,1,values));bad.mask[1][0][3]|=UINT64_C(1)<<32;saved=bad;
    assert(!fnv_d3d9_constants_get(c,0,0,7,1,before));assert(fnv_d3d9_constants_snapshot(c,&bad,0)==FNV_D3D9_INVALIDCALL);assert(!fnv_d3d9_constants_get(c,0,0,7,1,after) && !memcmp(before,after,16));
    assert(fnv_d3d9_constants_snapshot(c,&bad,1)==FNV_D3D9_INVALIDCALL && !memcmp(&bad,&saved,sizeof(bad)));
    assert(fnv_d3d9_constants_record(c,&bad,1,0,223,2,values)==FNV_D3D9_INVALIDCALL && !memcmp(&bad,&saved,sizeof(bad)));
    assert(fnv_d3d9_constants_record(c,&bad,2,0,0,1,values)==FNV_D3D9_INVALIDCALL && !memcmp(&bad,&saved,sizeof(bad)));
    fnv_d3d9_constants_destroy(c);
}
