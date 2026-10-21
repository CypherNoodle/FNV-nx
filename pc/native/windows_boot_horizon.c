#include "windows_boot_horizon.h"
#ifdef __SWITCH__
#include <switch.h>
static int create_window(void *context,const struct fnv_boot_window_request *request,void **out){
    struct fnv_boot_horizon *h=context;*out=NULL;
    if(!h || !request || !request->width || !request->height || !nwindowIsValid(h->nwindow))return 0;
    int expected=0;
    if(!atomic_compare_exchange_strong(&h->leased,&expected,1))return 0;
    *out=h->nwindow;return 1;
}
static void destroy_window(void *context,void *window){
    struct fnv_boot_horizon *h=context;if(h && h->nwindow==window)atomic_store(&h->leased,0);
}
int fnv_boot_horizon_backend(struct fnv_boot_horizon *h,void *window,struct fnv_boot_window_backend *out){
    if(!h || !out || !window || !nwindowIsValid(window))return 0;
    h->nwindow=window;atomic_init(&h->leased,0);
    *out=(struct fnv_boot_window_backend){h,create_window,destroy_window};return 1;
}
#else
int fnv_boot_horizon_backend(struct fnv_boot_horizon *h,void *window,struct fnv_boot_window_backend *out){
    (void)h;(void)window;(void)out;return 0;
}
#endif
