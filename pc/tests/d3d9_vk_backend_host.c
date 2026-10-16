/* Uses only the reusable CPU/COM harness below. The renamed NULLREF baseline
 * is never called; every resource/clear in this test is our native Vulkan. */
#define main unused_nullref_baseline
#include "d3d9_com_host.c"
#undef main
#include "d3d9_vk_backend.h"
#include "vk_backend_fault_proxy.h"
enum {RT=20,DS,DESC,SET_RT,GET_RT,SET_DS,GET_DS,CLEAR,TYPE,SET_VIEW,GET_VIEW,SET_SCISSOR,GET_SCISSOR,SET_RENDER,GET_RENDER,BEGIN,END};
static struct fnv_vk_backend *gpu;
static struct fnv_d3d9_shader_backend provider;
static unsigned pixels_checked;
static struct fnv_vk_stats stats(void){struct fnv_vk_stats s;assert(!fnv_vk_backend_stats(gpu,&s));return s;}
static uint32_t surface(int depth,unsigned size){
    uint32_t args[]={device_at,size,size,depth?75:21,0,0,0,GUEST_OUT,0};assert(!call(depth?DS:RT,9,args));return get(GUEST_OUT);
}
static void target(uint32_t color,uint32_t depth){
    uint32_t ds[]={device_at,0},rt[]={device_at,0,color};assert(!call(SET_DS,2,ds));assert(!call(SET_RT,3,rt));ds[1]=depth;assert(!call(SET_DS,2,ds));
}
static void clear_color(uint32_t color){uint32_t args[]={device_at,0,0,1,color,0x3f000000,0};assert(!call(CLEAR,7,args));}
static uintptr_t native_color(void){uintptr_t id=0;assert(!provider.get_target(gpu,0,0,&id));provider.release(gpu,id);return id;}
static void pixels(uint32_t base,uint32_t patch,unsigned w,unsigned h,unsigned x0,unsigned y0,unsigned x1,unsigned y1){
    unsigned char *data=malloc((size_t)w*h*4+16);assert(data);memset(data,0xa5,(size_t)w*h*4+16);
    assert(!fnv_vk_backend_read_color(gpu,native_color(),data,(size_t)w*h*4,10000000000ull));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint32_t actual;memcpy(&actual,data+(y*w+x)*4,4);
        assert(actual==((x>=x0 && x<x1 && y>=y0 && y<y1)?patch:base));pixels_checked++;}
    for(unsigned i=0;i<16;++i)assert(data[(size_t)w*h*4+i]==0xa5);
    free(data);
}
#include "vk_backend_buffers_fixture.h"
#include "vk_backend_textures_fixture.h"
#include "vk_backend_shaders_fixture.h"
#include "vk_backend_draw_fixture.h"
#include "vk_backend_com_present_fixture.h"
#include "vk_backend_factory_fixture.h"
#include "vk_backend_reset_fixture.h"
int main(int argc,char **argv){
    assert(argc==6 && (!strcmp(argv[3],"normal") || !strcmp(argv[3],"present") || !strcmp(argv[3],"wrong-clamp") || !strcmp(argv[3],"wrong-cull") || !strcmp(argv[3],"wrong-blend") || !strcmp(argv[3],"wrong-bc-alpha")));force_clamp=!strcmp(argv[3],"wrong-clamp");force_wrong_cull=!strcmp(argv[3],"wrong-cull");force_wrong_blend=!strcmp(argv[3],"wrong-blend");force_bc_opaque=!strcmp(argv[3],"wrong-bc-alpha");
    HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);
    FARPROC symbol=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&symbol,sizeof(gp));assert(gp);
    original_gpa=gp;gp=proxy_gpa;
    struct fnv_vk_config cfg={32,32,32,2,4,8u<<20,1};VkResult failure;
    struct fnv_vk_backend *bad=(void*)1;struct fnv_vk_config tiny=cfg;tiny.memory_budget=1;
    assert(fnv_vk_backend_create(gp,&tiny,&bad,&failure)==FNV_VK_OOM && !bad);
    force_noncoherent=1;
    if(!strcmp(argv[3],"present")){prepare_present_window();struct fnv_vk_window window={VK_KHR_WIN32_SURFACE_EXTENSION_NAME,present_window,com_window_surface,1};
        assert(!fnv_vk_backend_create_window(gp,&cfg,&window,&gpu,&failure));}
    else assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));
    force_noncoherent=0;fnv_vk_backend_callbacks(gpu,&provider);
    load_shader_catalog(argv[2]);register_shader_files(argv[4],argv[5]);
    original_declaration_create=provider.create_declaration;provider.create_declaration=capture_declaration;
    original_texture_create=provider.create_texture;provider.create_texture=capture_texture;
    original_buffer_create=provider.create_buffer;provider.create_buffer=capture_buffer;
    assert(stats().live_surfaces==2 && stats().device_references==1 && fnv_vk_backend_destroy(gpu)==FNV_VK_BUSY);
    arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
    struct fnv_cpu_config cpu_cfg={0};cpu_cfg.memory=arena;cpu_cfg.guest_base=BASE;cpu_cfg.memory_bytes=ARENA;cpu_cfg.code_low=BASE+0x1000;cpu_cfg.code_high=BASE+0x3000;
    assert(!fnv_cpu_create(&cpu_cfg,&cpu));registry_checks();code_at=BASE+0x200000;
    struct fnv_d3d9_factory_profile profile={32,32,21,75,60,gpu,factory_query};
    struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.backend=provider,
        .catalog=catalog,.catalog_count=catalog_count,.buffer_address=BASE+0x1000000,.buffer_bytes=4096,.factory=&profile,.presentation_timeout_ns=2000000000ull,.reset_timeout_ns=2000000000ull};
    struct fnv_d3d9_factory_profile bad_profile=profile;bad_profile.width=64;cc.factory=&bad_profile;
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_INVALID && !com);bad_profile=profile;bad_profile.depth_format=0;
    assert(fnv_d3d9_com_create(cpu,&cc,&com)==FNV_CPU_INVALID && !com);cc.factory=&profile;
    assert(!fnv_d3d9_com_create(cpu,&cc,&com));memset(&profile,0,sizeof(profile));
    size_t exports_count=0,bound_count=0;const struct fnv_cpu_export *exports=fnv_d3d9_com_exports(com,&exports_count);struct fnv_native_import imports[4];assert(exports_count==1);
    assert(!fnv_cpu_bind_imports(cpu,&image,imports,4,exports,exports_count,&bound_count) && bound_count==1);
    device_at=fnv_d3d9_com_device(com);
    struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));exercise_factory();exercise_reset();exercise_com_present();
    uint32_t back_args[]={device_at,0,GUEST_OUT},ds_args[]={device_at,GUEST_OUT};assert(!call(GET_RT,3,back_args));uint32_t back=get(GUEST_OUT);
    assert(!call(GET_DS,2,ds_args));uint32_t auto_ds=get(GUEST_OUT);
    uint32_t rt=surface(0,16),ds=surface(1,16);target(rt,ds);uintptr_t old_color=native_color();
    uint32_t desc[]={rt,DATA};assert(!call(DESC,2,desc) && get(DATA)==21 && get(DATA+24)==16 && call(TYPE,1,&rt)==1);
    uint32_t view[]={device_at,DATA};assert(!call(GET_VIEW,2,view) && get(DATA+8)==16 && get(DATA+12)==16);
    uint32_t clear[]={device_at,0,0,7,0xff13579b,0x3f000000,7};assert(!call(CLEAR,7,clear));
    const uint32_t rect[4]={2,3,6,7};memcpy(ptr(DATA),rect,16);clear[1]=1;clear[2]=DATA;clear[3]=1;clear[4]=0xffabcdef;assert(!call(CLEAR,7,clear));
    memset(ptr(DATA),0,16);pixels(0xff13579b,0xffabcdef,16,16,2,3,6,7);
    /* No implicit CPU/GPU fence at EndScene. Two submissions can be pending;
     * a third queued batch returns BUSY until an explicit completion check. */
    assert(!call(BEGIN,1,&device_at));clear_color(0xff112233);assert(!call(END,1,&device_at));assert(stats().queued_commands==1 && !stats().pending_frames);
    assert(!fnv_vk_backend_flush(gpu));clear_color(0xff445566);assert(!fnv_vk_backend_flush(gpu));clear_color(0xff778899);
    assert(fnv_vk_backend_flush(gpu)==FNV_VK_BUSY && stats().pending_frames==2 && stats().queued_commands==1);
    assert(!release(rt) && !release(ds));target(back,auto_ds);assert(stats().live_surfaces==4);
    assert(!fnv_vk_backend_wait(gpu,10000000000ull) && stats().live_surfaces==4); /* third batch still owns them */
    assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));assert(stats().live_surfaces==2);
    uint32_t untouched[8];memset(untouched,0xa5,sizeof(untouched));assert(provider.surface_desc(gpu,old_color,untouched)==FNV_VK_INVALID);
    for(unsigned i=0;i<8;++i)assert(untouched[i]==0xa5a5a5a5);
    /* Whole viewport, then intersection of a clipped clear, viewport and scissor. */
    clear_color(0xff010203);const uint32_t small_view[6]={1,2,8,6,0,0x3f800000};memcpy(ptr(DATA),small_view,24);assert(!call(SET_VIEW,2,view));
    const uint32_t scissor[4]={2,3,7,6};memcpy(ptr(DATA),scissor,16);assert(!call(SET_SCISSOR,2,view));
    uint32_t render[]={device_at,D3DRS_SCISSORTESTENABLE,1};assert(!call(SET_RENDER,3,render));
    const uint32_t clipped[4]={0xfffffffe,0xfffffffe,20,20};memcpy(ptr(DATA),clipped,16);
    clear[1]=1;clear[2]=DATA;clear[3]=1;clear[4]=0xff91a2b3;assert(!call(CLEAR,7,clear));pixels(0xff010203,0xff91a2b3,32,32,2,3,7,6);
    target(back,auto_ds);assert(!call(GET_SCISSOR,2,view) && !get(DATA) && get(DATA+8)==32);
    render[2]=0;assert(!call(SET_RENDER,3,render));
    /* Budget/caps and queue limits must not mutate live state. */
    struct fnv_vk_stats before=stats();uint32_t huge[]={device_at,4096,4096,21,0,0,0,GUEST_OUT,0};
    assert(call(RT,9,huge)==FNV_VK_OOM && !get(GUEST_OUT) && stats().allocated_bytes==before.allocated_bytes && stats().live_surfaces==before.live_surfaces);
    huge[1]=16;huge[2]=16;huge[3]=23;assert(call(RT,9,huge)==FNV_VK_INVALID && !get(GUEST_OUT));
    for(unsigned i=0;i<4;++i)clear_color(0xffabcdef);
    clear[1]=clear[2]=0;assert(call(CLEAR,7,clear)==FNV_VK_BUSY && stats().queued_commands==4);
    pixels(0xffabcdef,0xffabcdef,32,32,0,0,32,32);
    for(unsigned i=0;i<64;++i){uint32_t transient=surface(i&1,8);assert(!release(transient));}
    exercise_buffers();exercise_textures();exercise_shaders();exercise_draws();leave_shader_bound_for_close();
    assert(release(factory_public)==1);
    assert(!release(back) && !release(auto_ds));
    assert(!fnv_cpu_detach() && !fnv_d3d9_com_destroy(com));assert(!stats().device_references && !stats().allocated_bytes && !stats().live_surfaces && !stats().live_shaders && !stats().live_declarations);
    assert(!fnv_cpu_unregister_native(cpu,dummy_token,1) && !fnv_cpu_destroy(cpu));assert(VirtualFree(arena,0,MEM_RELEASE));
    struct fnv_vk_stats final=stats();assert(final.draws==37);assert(final.submissions==final.completions && !final.pending_frames && !final.queued_commands);
    uint32_t destroyed=FNV_VK_BUSY;for(unsigned i=0;i<100 && destroyed==FNV_VK_BUSY;++i){destroyed=fnv_vk_backend_destroy(gpu);if(destroyed==FNV_VK_BUSY)Sleep(1);}assert(!destroyed);
    if(present_window){DestroyWindow(present_window);UnregisterClassW(L"FNVCOMPresentProbe",GetModuleHandleW(NULL));}
    no_live_handles();exercise_draw_submit_faults(&cfg,argv[4],argv[5]);exercise_texture_submit_faults(&cfg);unsigned fault_cases=exercise_faults(&cfg);assert(fault_cases==9);
    exercise_reset_timeout(&cfg);
    hide_bc=1;struct fnv_vk_backend *no_bc;struct fnv_d3d9_shader_backend no_bc_provider;VkResult no_bc_reason;
    assert(!fnv_vk_backend_create(proxy_gpa,&cfg,&no_bc,&no_bc_reason));fnv_vk_backend_callbacks(no_bc,&no_bc_provider);hide_bc=0;
    for(unsigned i=0;i<3;++i){uintptr_t unchanged=123;assert(fnv_vk_backend_check_format(no_bc,0,3,bc_formats[i])==0x8876086au);assert(no_bc_provider.create_texture(no_bc,4,4,1,0,bc_formats[i],1,&unchanged)==FNV_VK_INVALID && unchanged==123);}
    no_bc_provider.device_release(no_bc);assert(!fnv_vk_backend_destroy(no_bc));no_live_handles();
    for(unsigned i=0;i<catalog_count;++i)free((void*)catalog[i].bytes);free(catalog);
    FreeLibrary(loader);assert(!api && !device);
    printf("{\"status\":\"PASS\",\"native_vulkan_backend\":true,\"desktop_d3d9_used\":false,\"gpu_rendered\":true,\"gpu_draw_executed\":true,\"game_frame_rendered\":false,"
        "\"fault_scenarios\":9,\"unknown_completion_quarantined\":true,\"noncoherent_invalidation_checked\":true,\"all_gpu_handles_released\":true,"
        "\"com_present_checks\":%u,\"com_wsi_presents\":%llu,"
        "\"reset_checks\":%u,\"successful_resets\":%llu,\"reset_timeout_checked\":true,"
        "\"factory_import_bound\":true,\"factory_checks\":%u,\"caps_complete\":false,\"prepared_create_device\":true,\"draws\":37,\"up_pixel_checks\":%u,\"up_checks\":%u,\"bc_pixel_checks\":%u,\"bc_transfer_checks\":%u,\"render_state_pixel_checks\":%u,\"draw_pixel_checks\":%u,\"draw_build_faults\":9,\"draw_submit_faults\":3,\"scaled_vertex_formats_checked\":3,\"native_shader_modules\":true,\"shader_module_faults\":2,\"uniform_bytes_checked\":96,\"declaration_owned_copy\":true,\"immutable_texture_versions\":true,\"texture_upload_faults\":5,\"texture_submit_faults\":3,\"texture_bytes_checked\":%u,"
        "\"immutable_buffer_versions\":true,\"buffer_upload_faults\":3,\"buffer_bytes_checked\":%u,"
        "\"pixel_checks\":%u,\"submissions\":%llu,\"completions\":%llu,\"frames_in_flight\":2,\"resources_retained_until_fence\":true,\"budget_and_backpressure_checked\":true}\n",
        com_present_checks,(unsigned long long)final.presentation_requests,reset_checks,(unsigned long long)final.resets,factory_checks,up_pixel_checks,up_checks,bc_pixel_checks,bc_transfer_checks,render_state_pixel_checks,draw_pixel_checks,texture_bytes_checked,buffer_bytes_checked,pixels_checked,(unsigned long long)final.submissions,(unsigned long long)final.completions);
    return 0;
}
