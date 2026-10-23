/* Reuse translated COM callers and native Vulkan helpers; baseline is not run. */
#define FNV_VK_HOST_MAIN unused_backend_baseline
#include "d3d9_vk_backend_host.c"
int main(int argc,char **argv){
    assert(argc==4);hardware_requested=!strcmp(argv[3],"hardware");
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC symbol=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&symbol,sizeof(gp));
    struct fnv_vk_config cfg={32,32,32,2,4,8u<<20,0};VkResult reason;
    assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&reason));fnv_vk_backend_callbacks(gpu,&provider);
    assert(fnv_vk_backend_presentation_format(NULL,22)==FNV_VK_INVALID);
    assert(fnv_vk_backend_presentation_format(gpu,23)==FNV_VK_INVALID);
    assert(!fnv_vk_backend_presentation_format(gpu,22));
    uintptr_t initial;assert(!provider.get_target(gpu,0,0,&initial));uint32_t desc[8];assert(!provider.surface_desc(gpu,initial,desc) && desc[0]==22);
    assert(fnv_vk_backend_presentation_format(gpu,21)==FNV_VK_INVALID);provider.release(gpu,initial);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_cpu_config cpu_cfg={0};cpu_cfg.memory=arena;cpu_cfg.guest_base=BASE;cpu_cfg.memory_bytes=ARENA;cpu_cfg.code_low=BASE+0x1000;cpu_cfg.code_high=BASE+0x3000;
    assert(!fnv_cpu_create(&cpu_cfg,&cpu));code_at=BASE+0x200000;
    struct fnv_d3d9_factory_profile profile={32,32,22,0,60,gpu,factory_query};
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.backend=provider,.factory=&profile,.reset_timeout_ns=2000000000ull};
    assert(!fnv_d3d9_com_create(cpu,&cc,&com));size_t n,bound=0;const struct fnv_cpu_export *exports=fnv_d3d9_com_exports(com,&n);struct fnv_native_import imports[4];
    assert(!fnv_cpu_bind_imports(cpu,&image,imports,4,exports,n,&bound) && bound==1);device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state state;fnv_cpu_state_init(&state,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&state));
    uint32_t version=32,factory=call(F_IMPORT,1,&version);assert(factory);caps_hwtl=hardware_requested;
    uint32_t pp[14]={32,32,22,1,0,0,1,0,0,0,75,0,0,1};memcpy(ptr(DATA),pp,56);
    uint32_t create[]={factory,0,1,0x1234,hardware_requested?0x84u:0x44u,DATA,GUEST_OUT};
    assert(!call(F_DEVICE,7,create) && get(GUEST_OUT)==device_at);
    uint32_t creation[]={device_at,DATA};assert(!call(F_CREATION,2,creation) && get(DATA+12)==create[4]);
    for(unsigned frame=0;frame<2;++frame){
        uint32_t rt[]={device_at,0,GUEST_OUT};assert(!call(GET_RT,3,rt));uint32_t back=get(GUEST_OUT);
        uint32_t description[]={back,DATA};assert(!call(DESC,2,description) && get(DATA)==22 && get(DATA+24)==32 && get(DATA+28)==32);
        uint32_t ds[]={device_at,GUEST_OUT};assert(call(GET_DS,2,ds)==0x88760866u && !get(GUEST_OUT));
        clear_color(frame?0xff123456u:0xffabcdefu);pixels(frame?0xff123456u:0xffabcdefu,0,32,32,0,0,0,0);
        assert(fnv_vk_backend_presentation_format(gpu,21)==FNV_VK_INVALID);assert(!release(back));
        if(!frame){memcpy(ptr(DATA),pp,56);uint32_t reset[]={device_at,DATA};assert(!call(F_RESET,2,reset));}
    }
    assert(release(factory)==1 && !release(device_at));assert(!fnv_cpu_detach());
    assert(!fnv_d3d9_com_destroy(com));assert(!fnv_cpu_destroy(cpu));assert(VirtualFree(arena,0,MEM_RELEASE));
    assert(!fnv_vk_backend_destroy(gpu));FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"hardware_creation\":%s,\"test_provider_caps_injected\":%s,\"production_caps_changed\":false,\"xrgb_backbuffer\":true,\"automatic_depth_disabled\":true,\"reset_preserves_format\":true,\"pixel_checks\":2048,\"game_frame_rendered\":false}\n",hardware_requested?"true":"false",hardware_requested?"true":"false");return 0;
}
