#include <vulkan/vulkan_win32.h>
enum {F_PRESENT=81};
static HWND present_window;
static unsigned com_present_checks;
static VkResult com_window_surface(void *window,VkInstance instance,PFN_vkGetInstanceProcAddr gp,VkSurfaceKHR *out){
    PFN_vkCreateWin32SurfaceKHR create=(PFN_vkCreateWin32SurfaceKHR)gp(instance,"vkCreateWin32SurfaceKHR");assert(create);
    VkWin32SurfaceCreateInfoKHR info={.sType=VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,.hinstance=GetModuleHandleW(NULL),.hwnd=window};
    return create(instance,&info,NULL,out);
}
static void prepare_present_window(void){
    WNDCLASSW wc={0};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(NULL);wc.lpszClassName=L"FNVCOMPresentProbe";assert(RegisterClassW(&wc));
    present_window=CreateWindowExW(0,wc.lpszClassName,L"FNV COM Present",WS_POPUP,0,0,32,32,NULL,NULL,wc.hInstance,NULL);assert(present_window);
    RECT client;assert(GetClientRect(present_window,&client) && client.right==32 && client.bottom==32);
}
static void exercise_com_present(void){
    uint32_t args[5]={device_at,0,0,0,0};uint64_t before=stats().presentation_requests;
    for(unsigned invalid=1;invalid<=4;++invalid){uint32_t bad[5];memcpy(bad,args,sizeof(bad));bad[invalid]=0xdeadbeef;
        assert(call(F_PRESENT,5,bad)==FNV_VK_INVALID);com_present_checks++;}
    uint32_t self=device_at;assert(!call(BEGIN,1,&self));assert(call(F_PRESENT,5,args)==FNV_VK_INVALID);assert(!call(END,1,&self));com_present_checks++;
    if(!present_window){assert(call(F_PRESENT,5,args)==0x8876086au);com_present_checks++;return;}
    uint32_t clear_args[]={device_at,0,0,1,0xff123456,0,0};assert(!call(CLEAR,7,clear_args));
    for(unsigned i=0;i<4;++i){args[3]=i&1?0x1234:0;assert(!call(F_PRESENT,5,args));com_present_checks++;}
    assert(stats().presentation_requests==before+4);assert(!fnv_vk_backend_wait(gpu,10000000000ull));
}
