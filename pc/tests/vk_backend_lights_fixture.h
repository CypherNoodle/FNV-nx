enum {LIGHT_SET=99,LIGHT_GET,LIGHT_ENABLE,LIGHT_ENABLED};
static uint32_t light_set(uint32_t id,const uint32_t words[26]){memcpy(ptr(DATA),words,104);uint32_t a[]={device_at,id,DATA};return call(LIGHT_SET,3,a);}
static uint32_t light_get(uint32_t id,uint32_t words[26]){uint32_t a[]={device_at,id,DATA};uint32_t hr=call(LIGHT_GET,3,a);if(!hr)memcpy(words,ptr(DATA),104);return hr;}
static void light_enable(uint32_t id,uint32_t enabled){uint32_t a[]={device_at,id,enabled};assert(!call(LIGHT_ENABLE,3,a));}
static uint32_t light_enabled(uint32_t id){uint32_t a[]={device_at,id,DATA};assert(!call(LIGHT_ENABLED,3,a));return get(DATA);}
static void exercise_lights(void){
    uint32_t defaults[26]={3,0x3f800000,0x3f800000,0x3f800000},words[26],out[26];defaults[18]=0x3f800000;
    memset(out,0xa5,104);assert(light_get(9,out)==FNV_VK_INVALID);for(unsigned i=0;i<26;i++)assert(out[i]==0xa5a5a5a5);
    light_enable(9,0);assert(!light_get(9,out) && !memcmp(defaults,out,104) && !light_enabled(9));
    light_enable(9,1);assert(light_enabled(9)==128);light_enable(9,0);
    memcpy(words,defaults,104);words[0]=4;words[18]=0x40000000;assert(!light_set(0xffffffff,words));assert(!light_get(0xffffffff,out) && !memcmp(words,out,104));
    assert(!call(SB_BEGIN,1,&device_at));words[18]=0x40a00000;assert(!light_set(0xffffffff,words));light_enable(0xffffffff,1);
    assert(!light_get(0xffffffff,out) && out[18]==0x40000000 && !light_enabled(0xffffffff));
    assert(!light_set(777,words));assert(!light_get(777,out) && !memcmp(defaults,out,104));
    light_enable(778,0);assert(!light_get(778,out) && !memcmp(defaults,out,104));
    uint32_t block=end_block();assert(!call(SB_APPLY,1,&block));assert(!light_get(0xffffffff,out) && !memcmp(words,out,104) && light_enabled(0xffffffff)==128);
    words[18]=0x40e00000;assert(!light_set(0xffffffff,words));light_enable(0xffffffff,0);assert(!call(SB_CAPTURE,1,&block));
    words[18]=0x41000000;assert(!light_set(0xffffffff,words));light_enable(0xffffffff,1);assert(!call(SB_APPLY,1,&block));
    assert(!light_get(0xffffffff,out) && out[18]==0x40e00000 && !light_enabled(0xffffffff));
    uint32_t a[]={device_at,9,0};assert(call(LIGHT_SET,3,a)==FNV_VK_INVALID && call(LIGHT_GET,3,a)==FNV_VK_INVALID && call(LIGHT_ENABLED,3,a)==FNV_VK_INVALID);
    a[2]=BASE+ARENA-100;memset(ptr(a[2]),0xa5,100);assert(call(LIGHT_SET,3,a)==FNV_VK_INVALID && call(LIGHT_GET,3,a)==FNV_VK_INVALID);
    for(unsigned i=0;i<100;i++)assert(ptr(a[2])[i]==0xa5);assert(!release(block));
}
