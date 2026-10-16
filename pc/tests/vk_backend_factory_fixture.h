enum {F_IMPORT=65,F_COUNT,F_MODE_COUNT,F_ENUM,F_DISPLAY,F_TYPE,F_FORMAT,F_CAPS,F_DEVICE,F_PARENT,F_CREATION,F_DEVICE_CAPS,F_MONITOR,F_UNAVAILABLE};
static uint32_t factory_public;
static unsigned factory_checks;
_Static_assert(sizeof(D3DCAPS9)==304,"D3DCAPS9 packed scalar ABI");
static uint32_t factory_query(void *ctx,uint32_t kind,const uint32_t *args,uint32_t count,void *output,uint32_t bytes){
    if(kind==0){assert(count==6 && !output && !bytes);return fnv_vk_backend_check_format(ctx,args[3],args[4],args[5]);}
    assert(kind==1 && count==2 && bytes==304);memset(output,0x33,bytes);return 0x8876086au; /* Full caps not implemented. */
}
static void exercise_factory(void){
    uint32_t version=31;assert(!call(F_IMPORT,1,&version));version=32;factory_public=call(F_IMPORT,1,&version);assert(factory_public && factory_public!=device_at);
    uint32_t factory=factory_public;assert(call(F_COUNT,1,&factory)==1);factory_checks++;
    const unsigned char iid[16]={0xca,0xcb,0xbd,0x81,0xd4,0x64,0x6d,0x42,0xae,0x8d,0xad,0x01,0x47,0xf4,0x27,0x5c};memcpy(ptr(DATA),iid,16);
    uint32_t query[]={factory,DATA,GUEST_OUT};assert(!call(QUERY,3,query) && get(GUEST_OUT)==factory);assert(release(factory)==2);
    uint32_t mode_count[]={factory,0,21};assert(call(F_MODE_COUNT,3,mode_count)==1);mode_count[1]=1;assert(!call(F_MODE_COUNT,3,mode_count));factory_checks+=2;
    uint32_t mode[]={factory,0,21,0,DATA};memset(ptr(DATA),0xa5,32);assert(!call(F_ENUM,5,mode));assert(get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==60 && get(DATA+12)==21 && get(DATA+16)==0xa5a5a5a5);
    memset(ptr(DATA),0xa5,16);mode[3]=1;assert(call(F_ENUM,5,mode)==FNV_VK_INVALID);for(unsigned i=0;i<4;++i)assert(get(DATA+i*4)==0xa5a5a5a5);factory_checks+=2;
    uint32_t display[]={factory,0,DATA};assert(!call(F_DISPLAY,3,display) && get(DATA)==32);
    uint32_t type[]={factory,0,1,21,21,1};assert(!call(F_TYPE,6,type));type[2]=2;assert(call(F_TYPE,6,type)==0x8876086au);factory_checks+=3;
    uint32_t format[]={factory,0,1,21,0,3,21};assert(!call(F_FORMAT,7,format));format[6]=0x31545844;assert(call(F_FORMAT,7,format)==0x8876086au);
    format[4]=1;format[5]=1;format[6]=21;assert(!call(F_FORMAT,7,format));format[4]=2;format[6]=75;assert(!call(F_FORMAT,7,format));factory_checks+=4;
    memset(ptr(DATA),0xa5,320);uint32_t caps[]={factory,0,1,DATA};assert(call(F_CAPS,4,caps)==0x8876086au);
    for(unsigned i=0;i<320;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;
    uint32_t monitor[]={factory,0};assert(!call(F_MONITOR,2,monitor));uint32_t software[]={factory,0};assert(call(F_UNAVAILABLE,2,software)==0x8876086au);factory_checks+=2;
    const uint32_t present[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};
    uint32_t create[]={factory,0,1,0x1234,0x46,DATA,GUEST_OUT};
    for(unsigned failure=0;failure<11;++failure){uint32_t params[14];memcpy(params,present,56);uint32_t args[7];memcpy(args,create,28);
        if(failure==0)args[1]=1;else if(failure==1)args[2]=2;else if(failure==2)args[4]=32;else if(failure==3)params[0]=64;
        else if(failure==4)params[4]=4;else if(failure==5)params[3]=2;else if(failure==6)params[10]=71;else if(failure==7)params[11]=1;
        else if(failure==8)params[13]=0x80000000;else if(failure==9)params[8]=0;else params[12]=30;
        memcpy(ptr(DATA),params,56);put(GUEST_OUT,0xa5a5a5a5);assert(call(F_DEVICE,7,args)!=0 && !get(GUEST_OUT));assert(!memcmp(ptr(DATA),params,56));factory_checks++;
    }
    memcpy(ptr(DATA),present,56);assert(!call(F_DEVICE,7,create));assert(get(GUEST_OUT)==device_at && get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==21 && get(DATA+12)==1);factory_checks++;
    uint32_t repeated[14];memcpy(repeated,ptr(DATA),56);assert(call(F_DEVICE,7,create)==FNV_VK_INVALID && !get(GUEST_OUT) && !memcmp(repeated,ptr(DATA),56));factory_checks++;
    uint32_t creation[]={device_at,DATA};memset(ptr(DATA),0xa5,32);assert(!call(F_CREATION,2,creation));assert(get(DATA)==0 && get(DATA+4)==1 && get(DATA+8)==0x1234 && get(DATA+12)==0x46 && get(DATA+16)==0xa5a5a5a5);factory_checks++;
    memset(ptr(DATA),0xa5,320);assert(call(F_DEVICE_CAPS,2,creation)==0x8876086au);for(unsigned i=0;i<320;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;
    assert(release(factory)==1);uint32_t parent[]={device_at,GUEST_OUT};assert(!call(F_PARENT,2,parent) && get(GUEST_OUT)==factory);assert(release(factory)==1);factory_checks++;
    factory_public=call(F_IMPORT,1,&version);assert(factory_public==factory);factory_checks++;
}
