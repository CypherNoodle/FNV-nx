/* This fixture checks the translated COM boundary only; pixel proof is separate. */
static void exercise_fixed_dispatch(uint32_t vs,uint32_t ps,uint32_t declaration){
    float saved[4][16],m[4][16];const uint32_t states[4]={256,2,3,16};
    for(unsigned i=0;i<4;i++){
        uint32_t a[]={device_at,states[i],DATA};assert(!call(95,3,a));memcpy(saved[i],ptr(DATA),64);
        memset(m[i],0,64);for(unsigned j=0;j<4;j++)m[i][j*5]=1;
    }
    m[0][0]=m[0][5]=.5f;m[0][12]=.25f;m[1][12]=-.25f;m[1][13]=.25f;m[2][0]=.5f;
    for(unsigned i=0;i<4;i++){memcpy(ptr(DATA),m[i],64);uint32_t a[]={device_at,states[i],DATA};assert(!call(94,3,a));}
    uint32_t bind[]={device_at,0},fvf[]={device_at,0x102};
    assert(!call(S_VS_BIND,2,bind) && !call(FVF_SET,2,fvf));
    fixed_probe_active=1;unsigned before=fixed_probe_calls;
    assert(!call(BEGIN,1,&device_at));
    uint32_t draw[]={device_at,4,1,2},indexed[]={device_at,4,3,2,6,1,2};
    assert(!call(D_PRIMITIVE,4,draw));assert(call(D_INDEXED,7,indexed)==FNV_VK_INVALID);
    assert(fixed_probe_calls==before+2);
    fvf[1]=0x112;assert(!call(FVF_SET,2,fvf));assert(call(D_PRIMITIVE,4,draw)==0x8876086au);
    fvf[1]=0x102;assert(!call(FVF_SET,2,fvf));
    m[3][12]=1;memcpy(ptr(DATA),m[3],64);uint32_t transform[]={device_at,16,DATA};assert(!call(94,3,transform));
    assert(call(D_PRIMITIVE,4,draw)==0x8876086au);m[3][12]=0;memcpy(ptr(DATA),m[3],64);assert(!call(94,3,transform));
    assert(!call(S_PS_BIND,2,bind));assert(call(D_PRIMITIVE,4,draw)==0x8876086au);
    assert(fixed_probe_calls==before+2);assert(!call(END,1,&device_at));fixed_probe_active=0;
    bind[1]=vs;assert(!call(S_VS_BIND,2,bind));bind[1]=ps;assert(!call(S_PS_BIND,2,bind));
    bind[1]=declaration;assert(!call(S_DECL_BIND,2,bind));
    for(unsigned i=0;i<4;i++){memcpy(ptr(DATA),saved[i],64);uint32_t a[]={device_at,states[i],DATA};assert(!call(94,3,a));}
}
