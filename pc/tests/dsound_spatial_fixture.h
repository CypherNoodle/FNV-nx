/* ABI oracle never starts a native Windows sound buffer. PCM remains in memory. */
static void spatial_compare(uint32_t spatial,IDirectSound3DBuffer *native){
    DS3DBUFFER expected={0};expected.dwSize=64;put(DATA,64);uint32_t args[]={spatial,DATA};
    assert(!IDirectSound3DBuffer_GetAllParameters(native,&expected) && !call(S_ALL_GET,args,2,0));
    for(unsigned k=0;k<16;k++){uint32_t v;memcpy(&v,(unsigned char*)&expected+k*4,4);
        if((k>=1 && k<=6) || (k>=9 && k<=11) || k==13 || k==14){float f,g;uint32_t actual=get(DATA+k*4);memcpy(&f,&actual,4);memcpy(&g,&v,4);assert(fabs((double)f-g)<0.000002);}else assert(get(DATA+k*4)==v);}
    native_checks++;
    DWORD inside,outside,mode;LONG volume;D3DVALUE minimum,maximum;D3DVECTOR vector;
    uint32_t scalar[]={spatial,DATA},angles[]={spatial,DATA,DATA+4};
    assert(!IDirectSound3DBuffer_GetConeAngles(native,&inside,&outside) && !call(S_ANGLES_GET,angles,3,0) && get(DATA)==inside && get(DATA+4)==outside);
    assert(!IDirectSound3DBuffer_GetMode(native,&mode) && !call(S_MODE_GET,scalar,2,0) && get(DATA)==mode);
    assert(!IDirectSound3DBuffer_GetConeOutsideVolume(native,&volume) && !call(S_OUTSIDE_GET,scalar,2,0) && get(DATA)==(uint32_t)volume);
    assert(!IDirectSound3DBuffer_GetMinDistance(native,&minimum) && !call(S_MIN_GET,scalar,2,0) && get(DATA)==bits(minimum));
    assert(!IDirectSound3DBuffer_GetMaxDistance(native,&maximum) && !call(S_MAX_GET,scalar,2,0) && get(DATA)==bits(maximum));
    for(unsigned j=0;j<3;j++){HRESULT hr=j==0?IDirectSound3DBuffer_GetPosition(native,&vector):j==1?IDirectSound3DBuffer_GetVelocity(native,&vector):IDirectSound3DBuffer_GetConeOrientation(native,&vector);
        assert(!hr && !call(j==0?S_POSITION_GET:j==1?S_VELOCITY_GET:S_ORIENTATION_GET,scalar,2,0));
        float *v=(float*)&vector;for(unsigned k=0;k<3;k++){uint32_t raw=get(DATA+k*4);float actual;memcpy(&actual,&raw,4);assert(fabs((double)actual-v[k])<0.000002);}}
    native_checks+=8;
}
static void spatial_test(uint32_t root,IDirectSound8 *native){
    wave(48000,1,16);uint32_t b=make(root,0x300b0,512);WAVEFORMATEX wave_format;memcpy(&wave_format,arena+FORMAT-BASE,18);
    DSBUFFERDESC desc={0};desc.dwSize=sizeof(desc);desc.dwFlags=0x300b0;desc.dwBufferBytes=512;desc.lpwfxFormat=&wave_format;
    IDirectSoundBuffer *nb=NULL;HRESULT create_hr=IDirectSound8_CreateSoundBuffer(native,&desc,&nb,NULL);fprintf(stderr,"native spatial create=%08lx\n",(unsigned long)create_hr);assert(SUCCEEDED(create_hr));
    IDirectSound3DBuffer *ns=NULL;assert(!IDirectSoundBuffer_QueryInterface(nb,&IID_IDirectSound3DBuffer,(void**)&ns));
    memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DBuffer,16);uint32_t qi[]={b,GUID_AT,OUTPUT_AT};assert(!call(QUERY,qi,3,0));uint32_t s=get(OUTPUT_AT);assert(s==b+4);
    spatial_compare(s,ns);
    const unsigned methods[]={S_POSITION_SET,S_VELOCITY_SET,S_ORIENTATION_SET};
    for(unsigned i=0;i<3;i++){uint32_t a[]={s,bits(1),bits(2),bits(3),0};HRESULT hr=i==0?IDirectSound3DBuffer_SetPosition(ns,1,2,3,0):i==1?IDirectSound3DBuffer_SetVelocity(ns,1,2,3,0):IDirectSound3DBuffer_SetConeOrientation(ns,1,2,3,0);
        assert(call(methods[i],a,5,0)==(uint32_t)hr);spatial_compare(s,ns);}
    uint32_t angles[]={s,90,180,0};assert(call(S_ANGLES_SET,angles,4,0)==(uint32_t)IDirectSound3DBuffer_SetConeAngles(ns,90,180,0));spatial_compare(s,ns);
    uint32_t scalar[]={s,(uint32_t)-1200,0};assert(call(S_OUTSIDE_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetConeOutsideVolume(ns,-1200,0));spatial_compare(s,ns);
    scalar[1]=bits(2);assert(call(S_MIN_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMinDistance(ns,2,0));scalar[1]=bits(50);assert(call(S_MAX_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMaxDistance(ns,50,0));spatial_compare(s,ns);
    scalar[1]=1;assert(call(S_MODE_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMode(ns,1,0));spatial_compare(s,ns);
    scalar[1]=3;assert(call(S_MODE_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMode(ns,3,0));
    angles[1]=361;assert(call(S_ANGLES_SET,angles,4,0)==(uint32_t)IDirectSound3DBuffer_SetConeAngles(ns,361,180,0));
    const float distances[]={0,-1,1.e-20f,3};
    for(unsigned k=0;k<4;k++){scalar[1]=bits(distances[k]);assert(call(S_MIN_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMinDistance(ns,distances[k],0));assert(call(S_MAX_SET,scalar,3,0)==(uint32_t)IDirectSound3DBuffer_SetMaxDistance(ns,distances[k],0));}
    DS3DBUFFER bulk={0};bulk.dwSize=64;bulk.vPosition.x=-3;bulk.vVelocity.y=2;bulk.dwInsideConeAngle=90;bulk.dwOutsideConeAngle=180;bulk.vConeOrientation.x=1;bulk.vConeOrientation.z=1;bulk.lConeOutsideVolume=-3000;bulk.flMinDistance=2;bulk.flMaxDistance=40;bulk.dwMode=1;
    memcpy(arena+DATA-BASE,&bulk,64);uint32_t bulk_args[]={s,DATA,0};assert(call(S_ALL_SET,bulk_args,3,0)==(uint32_t)IDirectSound3DBuffer_SetAllParameters(ns,&bulk,0));spatial_compare(s,ns);
    /* Stereo CTRL3D is invalid, and must not consume storage. */
    wave(48000,2,16);put(DESC,36);put(DESC+4,0x10);put(DESC+8,512);put(DESC+16,FORMAT);uint32_t create[]={root,DESC,OUTPUT_AT,0};assert(call(CREATE,create,4,0)==(uint32_t)DSERR_INVALIDPARAM);
    /* Listener commit makes all deferred voices visible atomically. */
    uint32_t primary=make(root,0x91,0);memcpy(arena+GUID_AT-BASE,&IID_IDirectSound3DListener,16);qi[0]=primary;assert(!call(QUERY,qi,3,0));uint32_t listener=get(OUTPUT_AT);
    uint32_t pos[]={s,bits(4),bits(5),bits(6),1};assert(!call(S_POSITION_SET,pos,5,0));uint32_t getpos[]={s,DATA};assert(!call(S_POSITION_GET,getpos,2,0) && get(DATA)==bits(-3));
    pos[1]=bits(7);pos[4]=0;assert(!call(S_POSITION_SET,pos,5,0));assert(!call(S_POSITION_GET,getpos,2,0) && get(DATA)==bits(7));assert(!call(L_COMMIT,&listener,1,0));assert(!call(S_POSITION_GET,getpos,2,0) && get(DATA)==bits(4));
    /* Known mono signal: distance attenuation, stereo direction and max mute. */
    int16_t pcm[256];for(unsigned i=0;i<256;i++)pcm[i]=8000;upload(b,0,512,(unsigned char*)pcm);
    DS3DBUFFER state={0};state.dwSize=64;state.dwInsideConeAngle=state.dwOutsideConeAngle=360;state.vConeOrientation.z=1;state.flMinDistance=1;state.flMaxDistance=10;state.dwMode=DS3DMODE_NORMAL;state.vPosition.z=1;
    memcpy(arena+DATA-BASE,&state,64);uint32_t all[]={s,DATA,0};assert(!call(S_ALL_SET,all,3,0));start(b,1);int16_t output[32];assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==8000);
    pos[1]=0;pos[2]=0;pos[3]=bits(2);assert(!call(S_POSITION_SET,pos,5,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==4000);
    pos[1]=bits(1);pos[3]=0;assert(!call(S_POSITION_SET,pos,5,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<16;i++)assert(output[i*2]==0 && output[i*2+1]==8000);
    pos[1]=0;pos[3]=bits(20);assert(!call(S_POSITION_SET,pos,5,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(!output[i]);
    scalar[1]=2;assert(!call(S_MODE_SET,scalar,3,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==8000);
    /* Cone rear attenuation, listener translation and head-relative mode. */
    state.vPosition.z=1;state.dwInsideConeAngle=90;state.dwOutsideConeAngle=180;state.lConeOutsideVolume=-2000;
    memcpy(arena+DATA-BASE,&state,64);assert(!call(S_ALL_SET,all,3,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==800);
    uint32_t orient[]={s,0,0,bits(-1),0};assert(!call(S_ORIENTATION_SET,orient,5,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==8000);
    uint32_t listener_pos[]={listener,bits(1),0,bits(1),0};assert(!call(L_POSITION_SET,listener_pos,5,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<16;i++)assert(output[i*2+1]==0);
    scalar[1]=1;assert(!call(S_MODE_SET,scalar,3,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);for(unsigned i=0;i<32;i++)assert(output[i]==8000);
    /* Source velocity changes the cursor; Doppler=0 suppresses it. */
    uint32_t velocity[]={s,0,0,bits(-100),0};assert(!call(S_VELOCITY_SET,velocity,5,0));uint32_t reset[]={b,0};assert(!call(SEEK,reset,2,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);assert(cursor(b)>32);
    uint32_t doppler[]={listener,0,0};assert(!call(L_DOPPLER_SET,doppler,3,0));assert(!call(SEEK,reset,2,0));assert(fnv_dsound8_pump(sound,16)==FNV_PCM_OK);read_exact(output,16);assert(cursor(b)==32);
    /* FULL queue preserves the spatial voice cursor as well. */
    int16_t silence[2048]={0};assert(fnv_pcm_write(queue,silence,1024)==FNV_PCM_OK);unsigned before=cursor(b);assert(fnv_dsound8_pump(sound,16)==FNV_PCM_FULL && cursor(b)==before);read_exact(silence,1024);
    uint32_t stop_args[]={b};assert(!call(STOP,stop_args,1,0));assert(call(RELEASE,&b,1,0)>0);release(s);assert(call(RELEASE,&primary,1,0)>0);release(listener);
    IDirectSound3DBuffer_Release(ns);IDirectSoundBuffer_Release(nb);sample_checks+=320;
    /* Retired COM identities never revive; released staging is reusable. */
    wave(48000,1,16);unsigned char marker[32];memset(marker,0x31,sizeof(marker));
    uint32_t held=make(root,0xb0,262144);upload(held,0,32,marker);
    for(unsigned i=0;i<40;i++){uint32_t temp=make(root,0xb0,262144);uint32_t lk[]={temp,0,32,DATA,DATA+4,DATA+8,DATA+12,0};assert(!call(LOCK,lk,8,0));for(unsigned k=0;k<32;k++)assert(!arena[get(DATA)-BASE+k]);uint32_t ul[]={temp,get(DATA),get(DATA+4),get(DATA+8),get(DATA+12)};assert(!call(UNLOCK,ul,5,0));unsigned char fill[32];memset(fill,(int)i,sizeof(fill));upload(temp,0,32,fill);release(temp);}
    uint32_t lock_args[]={held,0,32,DATA,DATA+4,DATA+8,DATA+12,0};assert(!call(LOCK,lock_args,8,0));assert(!memcmp(arena+get(DATA)-BASE,marker,32));uint32_t unlock_args[]={held,get(DATA),get(DATA+4),get(DATA+8),get(DATA+12)};assert(!call(UNLOCK,unlock_args,5,0));release(held);
    wave(48000,2,16);
}
