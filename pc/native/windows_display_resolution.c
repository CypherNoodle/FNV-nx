#include "windows_display_resolution.h"
#include <stddef.h>
#include <limits.h>
#ifdef __SWITCH__
#include <switch.h>
#endif
static int valid(const struct fnv_display_resolution *r){
    return r && r->mode<=1 && r->output_width && r->output_height && r->surface_width && r->surface_height &&
        r->output_width<=INT32_MAX && r->output_height<=INT32_MAX && r->surface_width<=INT32_MAX && r->surface_height<=INT32_MAX;
}
int fnv_display_resolution_horizon(void *window,struct fnv_display_resolution *out){
#ifdef __SWITCH__
    if(!window || !out || !nwindowIsValid(window))return 0;
    AppletOperationMode mode=appletGetOperationMode();
    if(mode!=AppletOperationMode_Handheld && mode!=AppletOperationMode_Console)return 0;
    s32 width=0,height=0;u32 surface_width=0,surface_height=0;
    if(R_FAILED(appletGetDefaultDisplayResolution(&width,&height)) || width<=0 || height<=0 ||
       R_FAILED(nwindowGetDimensions(window,&surface_width,&surface_height)) || mode!=appletGetOperationMode())return 0;
    struct fnv_display_resolution r={(uint32_t)mode,(uint32_t)width,(uint32_t)height,surface_width,surface_height};
    if(!valid(&r))return 0;
    *out=r;return 1;
#else
    (void)window;(void)out;return 0;
#endif
}
int fnv_display_resolution_metrics(void *context,uint32_t index,int32_t *out){
    /* SM_CYCAPTION is zero for the borderless guest desktop. */
    if(index!=0 && index!=1 && index!=4 && (index<76 || index>80))return 0;
    const struct fnv_display_resolution *r=context;if(!out || !valid(r))return -1;
    if(index==0 || index==78)*out=(int32_t)r->surface_width;
    else if(index==1 || index==79)*out=(int32_t)r->surface_height;
    else if(index==4)*out=0;
    else if(index==80)*out=1;
    else *out=0; /* The one prepared guest desktop begins at0,0. */
    return 1;
}
int fnv_display_resolution_changed(const struct fnv_display_resolution *a,const struct fnv_display_resolution *b){
    if(!valid(a) || !valid(b))return -1;
    return a->mode!=b->mode || a->output_width!=b->output_width || a->output_height!=b->output_height ||
        a->surface_width!=b->surface_width || a->surface_height!=b->surface_height;
}
