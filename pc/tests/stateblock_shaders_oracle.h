static unsigned shader_block_checks;
static void shader_blocks(IDirect3DDevice9 *d){
    const DWORD vs_code[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0xffff};
    const DWORD ps_code[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0xffff};
    for(unsigned round=0;round<8;++round){
        IDirect3DVertexShader9 *vs=NULL,*got_vs=NULL;IDirect3DPixelShader9 *ps=NULL,*got_ps=NULL;IDirect3DStateBlock9 *block=NULL;
        ok(IDirect3DDevice9_CreateVertexShader(d,vs_code,&vs));ok(IDirect3DDevice9_CreatePixelShader(d,ps_code,&ps));
        ok(IDirect3DDevice9_SetVertexShader(d,NULL));ok(IDirect3DDevice9_SetPixelShader(d,NULL));ok(IDirect3DDevice9_BeginStateBlock(d));
        ok(IDirect3DDevice9_SetVertexShader(d,vs));ok(IDirect3DDevice9_SetPixelShader(d,ps));ok(IDirect3DDevice9_SetPixelShader(d,NULL));ok(IDirect3DDevice9_SetPixelShader(d,ps));
        ok(IDirect3DDevice9_GetVertexShader(d,&got_vs));ok(IDirect3DDevice9_GetPixelShader(d,&got_ps));assert(!got_vs && !got_ps);shader_block_checks+=2;
        ok(IDirect3DDevice9_EndStateBlock(d,&block));assert(!IDirect3DVertexShader9_Release(vs));assert(!IDirect3DPixelShader9_Release(ps));
        ok(IDirect3DStateBlock9_Apply(block));ok(IDirect3DDevice9_GetVertexShader(d,&got_vs));ok(IDirect3DDevice9_GetPixelShader(d,&got_ps));assert(got_vs==vs && got_ps==ps);IDirect3DVertexShader9_Release(got_vs);IDirect3DPixelShader9_Release(got_ps);shader_block_checks+=2;
        ok(IDirect3DDevice9_SetVertexShader(d,NULL));ok(IDirect3DDevice9_SetPixelShader(d,NULL));ok(IDirect3DStateBlock9_Capture(block));ok(IDirect3DStateBlock9_Apply(block));
        ok(IDirect3DDevice9_GetVertexShader(d,&got_vs));ok(IDirect3DDevice9_GetPixelShader(d,&got_ps));assert(!got_vs && !got_ps);shader_block_checks+=2;IDirect3DStateBlock9_Release(block);
    }
}
