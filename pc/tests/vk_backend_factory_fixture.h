enum {F_IMPORT=65,F_COUNT,F_MODE_COUNT,F_ENUM,F_DISPLAY,F_TYPE,F_FORMAT,F_CAPS,F_DEVICE,F_PARENT,F_CREATION,F_DEVICE_CAPS,F_MONITOR,F_UNAVAILABLE,F_MSAA,F_DEPTH_MATCH};
enum {F_IDENTIFIER=93};
static uint32_t factory_public;
static unsigned factory_checks,identifier_failure,caps_failure,hardware_requested,caps_hwtl;
_Static_assert(sizeof(D3DCAPS9)==304,"D3DCAPS9 packed scalar ABI");
static uint32_t factory_query(void *ctx,uint32_t kind,const uint32_t *args,uint32_t count,void *output,uint32_t bytes){
    if(kind==2){assert(count==2 && bytes==1104);if(identifier_failure){memset(output,0x33,bytes);return 0x8876086au;}return fnv_vk_backend_adapter_identifier(ctx,output,bytes);}
    if(kind==0){assert(count==6 && !output && !bytes);return fnv_vk_backend_check_format(ctx,args[3],args[4],args[5]);}
    assert(kind==1 && count==2 && bytes==304);if(!caps_failure){uint32_t hr=fnv_vk_backend_device_caps(ctx,output,bytes);
        /* Test-only provider declaration; production Vulkan caps stay unchanged. */
        if(!hr && caps_hwtl)((unsigned char*)output)[30]|=1;
        return hr;}
    memset(output,0x33,bytes);return 0x8876086au;
}
/* Independent Vulkan query: the bridge must describe a real enumerated device. */
static void check_adapter_properties(PFN_vkGetInstanceProcAddr gp){
    PFN_vkCreateInstance create=(PFN_vkCreateInstance)gp(VK_NULL_HANDLE,"vkCreateInstance");assert(create);
    VkInstanceCreateInfo ci={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance;assert(create(&ci,NULL,&instance)==VK_SUCCESS);
    PFN_vkEnumeratePhysicalDevices enumerate=(PFN_vkEnumeratePhysicalDevices)gp(instance,"vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties properties=(PFN_vkGetPhysicalDeviceProperties)gp(instance,"vkGetPhysicalDeviceProperties");
    PFN_vkDestroyInstance destroy=(PFN_vkDestroyInstance)gp(instance,"vkDestroyInstance");assert(enumerate && properties && destroy);
    uint32_t count=0;assert(enumerate(instance,&count,NULL)==VK_SUCCESS && count);VkPhysicalDevice *devices=calloc(count,sizeof(*devices));assert(devices);
    assert(enumerate(instance,&count,devices)==VK_SUCCESS);unsigned char record[1104];assert(!fnv_vk_backend_adapter_identifier(gpu,record,sizeof(record)));unsigned matches=0;
    for(uint32_t i=0;i<count;++i){VkPhysicalDeviceProperties p;properties(devices[i],&p);uint32_t vendor,device;memcpy(&vendor,record+1064,4);memcpy(&device,record+1068,4);
        if(vendor==p.vendorID && device==p.deviceID && !strcmp((char*)record+512,p.deviceName) && !memcmp(record+1080,p.pipelineCacheUUID,16))matches++;}
    D3DCAPS9 caps;unsigned char untouched[304];assert(!fnv_vk_backend_device_caps(gpu,&caps,sizeof(caps)));
    for(uint32_t i=0;i<count;++i){VkPhysicalDeviceProperties p;properties(devices[i],&p);if(strcmp((char*)record+512,p.deviceName))continue;
        assert(caps.MaxTextureWidth==(p.limits.maxImageDimension2D<16384?p.limits.maxImageDimension2D:16384) && caps.MaxTextureHeight==caps.MaxTextureWidth);
        assert(caps.MaxStreamStride==p.limits.maxVertexInputBindingStride && caps.MaxStreams==(p.limits.maxVertexInputBindings<16?p.limits.maxVertexInputBindings:16));
        assert(caps.MaxVertexIndex==(p.limits.maxDrawIndexedIndexValue<0xffffffu?p.limits.maxDrawIndexedIndexValue:0xffffffu));
    }
    assert(caps.PrimitiveMiscCaps==(D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW|D3DPMISCCAPS_COLORWRITEENABLE|D3DPMISCCAPS_BLENDOP|D3DPMISCCAPS_SEPARATEALPHABLEND));
    assert(caps.TextureAddressCaps==(D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_INDEPENDENTUV));
    assert(caps.TextureCaps==(D3DPTEXTURECAPS_MIPMAP|D3DPTEXTURECAPS_ALPHA) && !caps.CubeTextureFilterCaps && !caps.VolumeTextureFilterCaps);
    assert(!caps.VertexShaderVersion && !caps.PixelShaderVersion && !caps.MaxVertexShaderConst && !caps.MaxActiveLights && !caps.StencilCaps && !caps.VertexTextureFilterCaps);
    memset(untouched,0xa5,sizeof(untouched));assert(fnv_vk_backend_device_caps(gpu,untouched,300)==FNV_VK_INVALID && fnv_vk_backend_device_caps(NULL,untouched,304)==FNV_VK_INVALID && fnv_vk_backend_device_caps(gpu,NULL,304)==FNV_VK_INVALID);
    for(unsigned i=0;i<sizeof(untouched);++i)assert(untouched[i]==0xa5);
    assert(matches);for(unsigned i=1056;i<1064;++i)assert(!record[i]);for(unsigned i=1072;i<1080;++i)assert(!record[i]);for(unsigned i=1096;i<1104;++i)assert(!record[i]);
    free(devices);destroy(instance,NULL);factory_checks++;
}
static void exercise_factory(void){
    uint32_t version=31;assert(!call(F_IMPORT,1,&version));version=32;factory_public=call(F_IMPORT,1,&version);assert(factory_public && factory_public!=device_at);
    _Static_assert(sizeof(D3DADAPTER_IDENTIFIER9)==1104,"adapter identifier x86-compatible layout");
    _Static_assert(offsetof(D3DADAPTER_IDENTIFIER9,VendorId)==1064 && offsetof(D3DADAPTER_IDENTIFIER9,WHQLLevel)==1096,"adapter identifier offsets");
    uint32_t ident[]={factory_public,0,0,DATA};unsigned char expected[1104];assert(!fnv_vk_backend_adapter_identifier(gpu,expected,sizeof(expected)));
    assert(!strcmp((char*)expected,"FNV-nx Vulkan") && expected[512] && !strcmp((char*)expected+1024,"\\\\.\\DISPLAY1"));
    for(unsigned flags=0;flags<=2;flags+=2){ident[2]=flags;memset(ptr(DATA),0xa5,1120);assert(!call(F_IDENTIFIER,4,ident));assert(!memcmp(ptr(DATA),expected,1104));for(unsigned i=1104;i<1120;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;}
    ident[1]=1;assert(call(F_IDENTIFIER,4,ident)==FNV_VK_INVALID);for(unsigned i=0;i<1104;++i)assert(!ptr(DATA)[i]);factory_checks++;
    ident[1]=0;ident[2]=1;memset(ptr(DATA),0xa5,1120);assert(call(F_IDENTIFIER,4,ident)==FNV_VK_INVALID);for(unsigned i=0;i<1120;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;
    identifier_failure=1;ident[2]=0;assert(call(F_IDENTIFIER,4,ident)==0x8876086au);for(unsigned i=0;i<1120;++i)assert(ptr(DATA)[i]==0xa5);identifier_failure=0;factory_checks++;
    ident[2]=0;ident[3]=0;assert(call(F_IDENTIFIER,4,ident)==FNV_VK_INVALID);factory_checks++;
    memset(expected,0xa5,sizeof(expected));assert(fnv_vk_backend_adapter_identifier(gpu,expected,1100)==FNV_VK_INVALID);for(unsigned i=0;i<sizeof(expected);++i)assert(expected[i]==0xa5);factory_checks++;
    uint32_t factory=factory_public;assert(call(F_COUNT,1,&factory)==1);factory_checks++;
    const unsigned char iid[16]={0xca,0xcb,0xbd,0x81,0xd4,0x64,0x6d,0x42,0xae,0x8d,0xad,0x01,0x47,0xf4,0x27,0x5c};memcpy(ptr(DATA),iid,16);
    uint32_t query[]={factory,DATA,GUEST_OUT};assert(!call(QUERY,3,query) && get(GUEST_OUT)==factory);assert(release(factory)==2);
    uint32_t mode_count[]={factory,0,21};assert(call(F_MODE_COUNT,3,mode_count)==1);mode_count[1]=1;assert(!call(F_MODE_COUNT,3,mode_count));factory_checks+=2;
    uint32_t mode[]={factory,0,21,0,DATA};memset(ptr(DATA),0xa5,32);assert(!call(F_ENUM,5,mode));assert(get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==60 && get(DATA+12)==21 && get(DATA+16)==0xa5a5a5a5);
    memset(ptr(DATA),0xa5,16);mode[3]=1;assert(call(F_ENUM,5,mode)==FNV_VK_INVALID);for(unsigned i=0;i<4;++i)assert(get(DATA+i*4)==0xa5a5a5a5);factory_checks+=2;
    /* RGB display modes ignore alpha; target storage stays ARGB. */
    mode_count[1]=0;mode_count[2]=22;assert(call(F_MODE_COUNT,3,mode_count)==1);
    mode[2]=22;mode[3]=0;memset(ptr(DATA),0xa5,32);assert(!call(F_ENUM,5,mode));
    assert(get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==60 && get(DATA+12)==22 && get(DATA+16)==0xa5a5a5a5);
    mode_count[2]=23;assert(!call(F_MODE_COUNT,3,mode_count));
    mode[2]=23;memset(ptr(DATA),0xa5,32);assert(call(F_ENUM,5,mode)==FNV_VK_INVALID);
    for(unsigned i=0;i<32;i++)assert(ptr(DATA)[i]==0xa5);
    uint32_t rgb_type[]={factory,0,1,22,21,1};assert(!call(F_TYPE,6,rgb_type));
    rgb_type[2]=2;assert(call(F_TYPE,6,rgb_type)==0x8876086au);
    uint32_t rgb_format[]={factory,0,1,22,1,1,21};assert(!call(F_FORMAT,7,rgb_format));
    rgb_format[6]=22;assert(call(F_FORMAT,7,rgb_format)==0x8876086au);
    uint32_t rgb_match[]={factory,0,1,22,21,75};assert(!call(F_DEPTH_MATCH,6,rgb_match));
    rgb_match[4]=22;assert(call(F_DEPTH_MATCH,6,rgb_match)==0x8876086au);
    factory_checks+=10;
    uint32_t display[]={factory,0,DATA};assert(!call(F_DISPLAY,3,display) && get(DATA)==32);
    uint32_t type[]={factory,0,1,21,21,1};assert(!call(F_TYPE,6,type));type[2]=2;assert(call(F_TYPE,6,type)==0x8876086au);factory_checks+=3;
    uint32_t format[]={factory,0,1,21,0,3,21};assert(!call(F_FORMAT,7,format));format[6]=0x31545844;assert(!call(F_FORMAT,7,format));
    format[4]=1;format[5]=1;format[6]=21;assert(!call(F_FORMAT,7,format));format[4]=2;format[6]=75;assert(!call(F_FORMAT,7,format));factory_checks+=4;
    caps_failure=1;memset(ptr(DATA),0xa5,320);uint32_t caps[]={factory,0,1,DATA};assert(call(F_CAPS,4,caps)==0x8876086au);
    for(unsigned i=0;i<320;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;
    caps_failure=0;assert(!call(F_CAPS,4,caps));D3DCAPS9 actual_caps;memcpy(&actual_caps,ptr(DATA),sizeof(actual_caps));
    assert(actual_caps.DeviceType==D3DDEVTYPE_HAL && !actual_caps.AdapterOrdinal && actual_caps.MaxStreams>0 && actual_caps.MaxStreams<=16 && actual_caps.NumSimultaneousRTs==1);
    assert(!actual_caps.VertexShaderVersion && !actual_caps.PixelShaderVersion && !actual_caps.TextureOpCaps && !actual_caps.MaxTextureBlendStages && !actual_caps.MaxSimultaneousTextures);
    assert(!actual_caps.StencilCaps && actual_caps.MaxAnisotropy==1 && !(actual_caps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT));factory_checks++;
    caps[1]=1;memset(ptr(DATA),0xa5,320);assert(call(F_CAPS,4,caps)==FNV_VK_INVALID);for(unsigned i=0;i<320;++i)assert(ptr(DATA)[i]==0xa5);caps[1]=0;factory_checks++;
    caps[2]=2;assert(call(F_CAPS,4,caps)==0x8876086au);caps[2]=1;factory_checks++;
    caps[3]=0;assert(call(F_CAPS,4,caps)==FNV_VK_INVALID);caps[3]=DATA;factory_checks++;
    caps_failure=1;
    uint32_t monitor[]={factory,0};assert(!call(F_MONITOR,2,monitor));uint32_t software[]={factory,0};assert(call(F_UNAVAILABLE,2,software)==0x8876086au);factory_checks+=2;
    uint32_t msaa[]={factory,0,1,21,1,0,DATA};put(DATA,0xa5a5a5a5);put(DATA+4,0xa5a5a5a5);
    assert(!call(F_MSAA,7,msaa) && get(DATA)==1 && get(DATA+4)==0xa5a5a5a5);factory_checks++;
    msaa[3]=75;msaa[4]=0;msaa[6]=0;assert(!call(F_MSAA,7,msaa));factory_checks++;
    msaa[6]=DATA;
    for(unsigned samples=1;samples<=17;++samples){msaa[5]=samples;put(DATA,0xa5a5a5a5);
        assert(call(F_MSAA,7,msaa)==(samples>16?FNV_VK_INVALID:0x8876086au) && get(DATA)==0xa5a5a5a5);factory_checks++;}
    msaa[5]=0;msaa[1]=1;assert(call(F_MSAA,7,msaa)==FNV_VK_INVALID);factory_checks++;
    msaa[1]=0;msaa[3]=22;assert(call(F_MSAA,7,msaa)==0x8876086au);factory_checks++;
    uint32_t match[]={factory,0,1,21,21,75};assert(!call(F_DEPTH_MATCH,6,match));factory_checks++;
    match[5]=71;assert(call(F_DEPTH_MATCH,6,match)==0x8876086au);factory_checks++;
    match[5]=75;match[4]=22;assert(call(F_DEPTH_MATCH,6,match)==0x8876086au);factory_checks++;
    match[4]=21;match[1]=1;assert(call(F_DEPTH_MATCH,6,match)==FNV_VK_INVALID);factory_checks++;
    const uint32_t present[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};
    uint32_t create[]={factory,0,1,0x1234,0x46,DATA,GUEST_OUT};
    for(unsigned failure=0;failure<5;++failure){
        uint32_t args[7];memcpy(args,create,28);args[4]=failure==0?0x80u:failure==1?0xc0u:failure==2?0x100u:0x80u;
        caps_failure=failure==3;caps_hwtl=failure>=3;
        if(failure==4)args[4]|=0x10u; /* pure devices remain unsupported */
        memcpy(ptr(DATA),present,56);put(GUEST_OUT,0xa5a5a5a5);
        uint32_t expected=failure==0 || failure==3?0x8876086au:FNV_VK_INVALID;
        assert(call(F_DEVICE,7,args)==expected && !get(GUEST_OUT) && !memcmp(ptr(DATA),present,56));factory_checks++;
    }
    caps_failure=0;caps_hwtl=0;
    for(unsigned failure=0;failure<11;++failure){uint32_t params[14];memcpy(params,present,56);uint32_t args[7];memcpy(args,create,28);
        if(failure==0)args[1]=1;else if(failure==1)args[2]=2;else if(failure==2)args[4]=32;else if(failure==3)params[0]=64;
        else if(failure==4)params[4]=4;else if(failure==5)params[3]=2;else if(failure==6)params[10]=71;else if(failure==7)params[11]=1;
        else if(failure==8)params[13]=0x80000000;else if(failure==9)params[8]=0;else params[12]=30;
        memcpy(ptr(DATA),params,56);put(GUEST_OUT,0xa5a5a5a5);assert(call(F_DEVICE,7,args)!=0 && !get(GUEST_OUT));assert(!memcmp(ptr(DATA),params,56));factory_checks++;
    }
    if(hardware_requested){create[4]=0x46;caps_hwtl=1;actual_caps.DevCaps|=D3DDEVCAPS_HWTRANSFORMANDLIGHT;}
    memcpy(ptr(DATA),present,56);assert(!call(F_DEVICE,7,create));assert(get(GUEST_OUT)==device_at && get(DATA)==32 && get(DATA+4)==32 && get(DATA+8)==21 && get(DATA+12)==1);factory_checks++;
    uint32_t repeated[14];memcpy(repeated,ptr(DATA),56);assert(call(F_DEVICE,7,create)==FNV_VK_INVALID && !get(GUEST_OUT) && !memcmp(repeated,ptr(DATA),56));factory_checks++;
    uint32_t creation[]={device_at,DATA};memset(ptr(DATA),0xa5,32);assert(!call(F_CREATION,2,creation));assert(get(DATA)==0 && get(DATA+4)==1 && get(DATA+8)==0x1234 && get(DATA+12)==create[4] && get(DATA+16)==0xa5a5a5a5);factory_checks++;
    caps_failure=1;memset(ptr(DATA),0xa5,320);assert(call(F_DEVICE_CAPS,2,creation)==0x8876086au);for(unsigned i=0;i<320;++i)assert(ptr(DATA)[i]==0xa5);factory_checks++;
    caps_failure=0;assert(!call(F_DEVICE_CAPS,2,creation));assert(!memcmp(ptr(DATA),&actual_caps,sizeof(actual_caps)));for(unsigned i=304;i<320;++i)assert(ptr(DATA)[i]==0xa5);caps_failure=1;factory_checks++;
    assert(release(factory)==1);uint32_t parent[]={device_at,GUEST_OUT};assert(!call(F_PARENT,2,parent) && get(GUEST_OUT)==factory);assert(release(factory)==1);factory_checks++;
    factory_public=call(F_IMPORT,1,&version);assert(factory_public==factory);factory_checks++;
}
