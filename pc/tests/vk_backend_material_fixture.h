enum {MATERIAL_SET=97,MATERIAL_GET};
static void material_set(const uint32_t m[17]){memcpy(ptr(DATA),m,68);uint32_t a[]={device_at,DATA};assert(!call(MATERIAL_SET,2,a));}
static void material_get(uint32_t m[17]){uint32_t a[]={device_at,DATA};assert(!call(MATERIAL_GET,2,a));memcpy(m,ptr(DATA),68);}
static void exercise_material(void){
    uint32_t zero[17]={0},live[17],recorded[17],actual[17];material_get(actual);assert(!memcmp(zero,actual,68));
    for(unsigned i=0;i<17;i++){live[i]=0x3f000000+i;recorded[i]=0x3e800000+i;}
    material_set(live);assert(!call(SB_BEGIN,1,&device_at));material_set(recorded);material_get(actual);assert(!memcmp(live,actual,68));
    uint32_t block=end_block();assert(!call(SB_APPLY,1,&block));material_get(actual);assert(!memcmp(recorded,actual,68));
    material_set(live);assert(!call(SB_CAPTURE,1,&block));material_set(zero);assert(!call(SB_APPLY,1,&block));material_get(actual);assert(!memcmp(live,actual,68));
    uint32_t a[]={device_at,0};assert(call(MATERIAL_SET,2,a)==FNV_VK_INVALID && call(MATERIAL_GET,2,a)==FNV_VK_INVALID);
    a[1]=BASE+ARENA-64;memset(ptr(a[1]),0xa5,64);assert(call(MATERIAL_SET,2,a)==FNV_VK_INVALID && call(MATERIAL_GET,2,a)==FNV_VK_INVALID);
    for(unsigned i=0;i<64;i++)assert(ptr(a[1])[i]==0xa5);
    material_get(actual);assert(!memcmp(live,actual,68));assert(!release(block));material_set(zero);
}
