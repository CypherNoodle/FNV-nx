static unsigned texture_block_checks;
static void texture_blocks(IDirect3DDevice9 *d){
    for(unsigned round=0;round<8;++round){
        IDirect3DTexture9 *a=NULL,*b=NULL;IDirect3DStateBlock9 *block=NULL;
        ok(IDirect3DDevice9_CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&a,NULL));
        ok(IDirect3DDevice9_CreateTexture(d,2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&b,NULL));
        for(unsigned i=0;i<16;++i)ok(IDirect3DDevice9_SetTexture(d,i,NULL));
        ok(IDirect3DDevice9_BeginStateBlock(d));
        for(unsigned i=0;i<16;++i)ok(IDirect3DDevice9_SetTexture(d,i,(IDirect3DBaseTexture9*)a));
        ok(IDirect3DDevice9_SetTexture(d,15,NULL));
        for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9 *got=(void*)1;ok(IDirect3DDevice9_GetTexture(d,i,&got));assert(!got);texture_block_checks++;}
        ok(IDirect3DDevice9_EndStateBlock(d,&block));assert(!IDirect3DTexture9_Release(a));
        ok(IDirect3DStateBlock9_Apply(block));
        for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9 *got=NULL;ok(IDirect3DDevice9_GetTexture(d,i,&got));assert(got==(i==15?NULL:(IDirect3DBaseTexture9*)a));if(got)IDirect3DBaseTexture9_Release(got);texture_block_checks++;}
        for(unsigned i=0;i<16;++i)ok(IDirect3DDevice9_SetTexture(d,i,(IDirect3DBaseTexture9*)b));
        ok(IDirect3DStateBlock9_Capture(block));for(unsigned i=0;i<16;++i)ok(IDirect3DDevice9_SetTexture(d,i,NULL));
        assert(!IDirect3DTexture9_Release(b));ok(IDirect3DStateBlock9_Apply(block));IDirect3DStateBlock9_Release(block);
        for(unsigned i=0;i<16;++i){IDirect3DBaseTexture9 *got=NULL;ok(IDirect3DDevice9_GetTexture(d,i,&got));assert(got==(IDirect3DBaseTexture9*)b);IDirect3DBaseTexture9_Release(got);texture_block_checks++;ok(IDirect3DDevice9_SetTexture(d,i,NULL));}
    }
}
