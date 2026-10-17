/* Reuse the own translated-caller harness, not its desktop-only test main. */
#define FNV_VK_HOST_MAIN unused_renderer_regression_main
#include "d3d9_vk_backend_host.c"
#include "../switch/renderer_probe_checks.h"
static int probe_host_check(uint32_t hr,const char *step){if(hr)fprintf(stderr,"probe FAIL %s hr=%08x\n",step,hr);return !hr;}
int main(int argc,char **argv){
    assert(argc==6);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&raw,sizeof(gp));assert(gp);original_gpa=gp;
    struct fnv_vk_config cfg={32,32,32,2,8,64u<<20,1};VkResult reason;
    if(!strcmp(argv[3],"present")){prepare_present_window();struct fnv_vk_window w={VK_KHR_WIN32_SURFACE_EXTENSION_NAME,present_window,com_window_surface,1};assert(!fnv_vk_backend_create_window(proxy_gpa,&cfg,&w,&gpu,&reason));}
    else assert(!fnv_vk_backend_create(proxy_gpa,&cfg,&gpu,&reason));
    fnv_vk_backend_callbacks(gpu,&provider);load_shader_catalog(argv[2]);register_shader_files(argv[4],argv[5]);
    original_texture_create=provider.create_texture;provider.create_texture=capture_texture;
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_cpu_config cpu_cfg={.memory=arena,.guest_base=BASE,.memory_bytes=ARENA,.code_low=BASE+0x1000,.code_high=BASE+0x3000};assert(!fnv_cpu_create(&cpu_cfg,&cpu));code_at=BASE+0x200000;
    struct fnv_d3d9_factory_profile profile={32,32,21,75,60,gpu,factory_query};
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.backend=provider,.catalog=catalog,.catalog_count=catalog_count,.buffer_address=BASE+0x1000000,.buffer_bytes=65536,.factory=&profile,.presentation_timeout_ns=2000000000ull,.reset_timeout_ns=2000000000ull};assert(!fnv_d3d9_com_create(cpu,&cc,&com));device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));
    uint32_t parent[]={device_at,GUEST_OUT};assert(!call(74,2,parent));uint32_t factory=get(GUEST_OUT);const uint32_t params[14]={0,0,0,0,0,0,1,0,1,1,75,0,0,1};memcpy(ptr(DATA),params,56);uint32_t create[]={factory,0,1,0x1234,0x46,DATA,GUEST_OUT};assert(!call(73,7,create));release(factory);
    unsigned stages[2]={0};for(unsigned i=0;i<catalog_count;++i)stages[((const uint32_t*)catalog[i].bytes)[0]>>16==0xffff]=i;
    struct fnv_vk_vertex_input map[2]={{0,0,0},{5,0,1}};assert(!fnv_vk_backend_link_program(gpu,catalog[stages[0]].bytes,catalog[stages[0]].size,catalog[stages[1]].bytes,catalog[stages[1]].size,map,2,1));
    struct fnv_probe_scene scene={.vs=make_shader(stages[0]),.ps=make_shader(stages[1]),.vb=make_buffer(0,96,0,1,0),.ib=make_buffer(1,16,0,1,101),.texture=make_texture(2,2,1,0,21,1)};
    const unsigned char elements[24]={0,0,0,0,1,0,0,0,0,0,8,0,1,0,5,0,255,0,0,0,17,0,0,0};memcpy(ptr(DATA),elements,24);uint32_t decl[]={device_at,DATA,GUEST_OUT};assert(!call(57,3,decl));scene.declaration=get(GUEST_OUT);draw_geometry(scene.vb,scene.ib,0);const unsigned char gray[4]={255,128,64,192};draw_texture(scene.texture,gray);texture_ready(scene.texture);
    struct fnv_probe_checks probe={.gpu=gpu,.provider=&provider,.device=device_at,.data=DATA,.out=GUEST_OUT,.log=stdout,.call=call,.memory=ptr,.check=probe_host_check};assert(fnv_renderer_probe_checks(&probe,&scene));exercise_com_present();
    assert(!fnv_vk_backend_wait(gpu,2000000000ull));assert(!fnv_cpu_detach());assert(!fnv_d3d9_com_destroy(com));assert(!fnv_cpu_destroy(cpu));assert(VirtualFree(arena,0,MEM_RELEASE));struct fnv_vk_stats final=stats();assert(!final.allocated_bytes && !final.device_references && final.draws==9);assert(!fnv_vk_backend_destroy(gpu));no_live_handles();
    if(present_window){DestroyWindow(present_window);UnregisterClassW(L"FNVCOMPresentProbe",GetModuleHandleW(NULL));}
    for(unsigned i=0;i<catalog_count;++i)free((void*)catalog[i].bytes);free(catalog);FreeLibrary(loader);
    printf("{\"status\":\"PASS\",\"native_vulkan_backend\":true,\"gpu_rendered\":true,\"probe_groups\":%u,\"probe_pixels\":%u,\"probe_draws\":%u,\"com_wsi_presents\":%llu,\"all_gpu_handles_released\":true}\n",probe.groups,probe.pixels,probe.draws,(unsigned long long)final.presentation_requests);return 0;
}
