enum {TR_SET=94,TR_GET,TR_MULTIPLY};
static void matrix_set(uint32_t state,const float m[16]){
    memcpy(ptr(DATA),m,64);uint32_t a[]={device_at,state,DATA};assert(!call(TR_SET,3,a));
}
static void matrix_get(uint32_t state,float m[16]){
    uint32_t a[]={device_at,state,DATA};assert(!call(TR_GET,3,a));memcpy(m,ptr(DATA),64);
}
static void matrix_identity(float m[16]){memset(m,0,64);for(unsigned i=0;i<4;i++)m[i*5]=1;}
static void exercise_transforms(void){
    float m[16],actual[16],identity[16];matrix_identity(identity);
    for(unsigned state=0;state<512;state++){
        if(fnv_transform_index(state)<0)continue;
        matrix_get(state,actual);assert(!memcmp(identity,actual,64));
        for(unsigned i=0;i<16;i++)m[i]=(float)((int)((state+i)%7)-3);
        matrix_set(state,m);matrix_get(state,actual);assert(!memcmp(m,actual,64));
    }
    memcpy(m,identity,64);m[12]=2;matrix_set(256,m);
    assert(!call(SB_BEGIN,1,&device_at));m[12]=5;matrix_set(256,m);
    matrix_get(256,actual);assert(actual[12]==2);
    memcpy(m,identity,64);m[0]=3;memcpy(ptr(DATA),m,64);
    uint32_t a[]={device_at,256,DATA};assert(!call(TR_MULTIPLY,3,a));
    matrix_get(256,actual);assert(actual[0]==3 && actual[12]==2);
    uint32_t block=end_block();assert(!call(SB_APPLY,1,&block));
    matrix_get(256,actual);assert(actual[0]==1 && actual[12]==5);
    m[0]=7;m[12]=9;matrix_set(256,m);assert(!call(SB_CAPTURE,1,&block));
    matrix_set(256,identity);assert(!call(SB_APPLY,1,&block));
    matrix_get(256,actual);assert(!memcmp(m,actual,64));assert(!release(block));
    a[1]=0;memset(ptr(DATA),0xa5,64);assert(call(TR_GET,3,a)==FNV_VK_INVALID);
    for(unsigned i=0;i<64;i++)assert(ptr(DATA)[i]==0xa5);
    assert(call(TR_SET,3,a)==FNV_VK_INVALID && call(TR_MULTIPLY,3,a)==FNV_VK_INVALID);
    a[1]=256;a[2]=0;assert(call(TR_GET,3,a)==FNV_VK_INVALID && call(TR_SET,3,a)==FNV_VK_INVALID);
    matrix_get(256,actual);assert(!memcmp(m,actual,64));
}
