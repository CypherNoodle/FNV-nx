#include <switch.h>
#include "media_horizon.h"
#include "host_mutex.h"
#include <stdlib.h>
struct fnv_horizon_pad {PadState pad;struct fnv_xinput *input;fnv_host_mutex guard;HidVibrationDeviceHandle vibration[2];int labels,ready;uint32_t style,id;uint16_t left,right;};
enum fnv_cpu_result fnv_horizon_pad_clear(struct fnv_horizon_pad *p){
    if(!p)return FNV_CPU_INVALID;
    const struct fnv_gamepad neutral={0};return fnv_xinput_update(p->input,0,padIsConnected(&p->pad),&neutral);
}
static uint32_t rumble(void *context,uint32_t user,uint16_t left,uint16_t right){
    struct fnv_horizon_pad *p=context;if(user)return 1167;
    fnv_host_mutex_lock(&p->guard);uint32_t r=p->ready?0:50;
    if(!r){p->left=left;p->right=right;}fnv_host_mutex_unlock(&p->guard);return r;
}
static int16_t axis(s32 v){return (int16_t)(v<-32768?-32768:v>32767?32767:v);}
enum fnv_cpu_result fnv_horizon_pad_open(struct fnv_windows_process *process,int labels,struct fnv_horizon_pad **out){
    if(!out)return FNV_CPU_INVALID;
    *out=NULL;struct fnv_horizon_pad *p=calloc(1,sizeof(*p));if(!p)return FNV_CPU_NO_MEMORY;
    fnv_host_mutex_init(&p->guard);p->labels=!!labels;
    padConfigureInput(1,HidNpadStyleSet_NpadStandard);padInitializeDefault(&p->pad);
    struct fnv_xinput_config config={p,rumble};enum fnv_cpu_result r=fnv_xinput_create(process,&config,&p->input);
    if(r!=FNV_CPU_OK){fnv_host_mutex_destroy(&p->guard);free(p);return r;}
    r=fnv_horizon_pad_poll(p);
    if(r!=FNV_CPU_OK){(void)fnv_xinput_destroy(p->input);fnv_host_mutex_destroy(&p->guard);free(p);return r;}
    *out=p;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_horizon_pad_poll(struct fnv_horizon_pad *p){
    if(!p)return FNV_CPU_INVALID;
    padUpdate(&p->pad);u64 b=padGetButtons(&p->pad);struct fnv_gamepad g={0};
    const u64 buttons[]={HidNpadButton_Up,HidNpadButton_Down,HidNpadButton_Left,HidNpadButton_Right,
        HidNpadButton_Plus,HidNpadButton_Minus,HidNpadButton_StickL,HidNpadButton_StickR,HidNpadButton_L,HidNpadButton_R};
    for(unsigned i=0;i<10;i++)if(b&buttons[i])g.buttons|=(uint16_t)(1u<<i);
    u64 a=p->labels?HidNpadButton_A:HidNpadButton_B,bb=p->labels?HidNpadButton_B:HidNpadButton_A;
    u64 x=p->labels?HidNpadButton_X:HidNpadButton_Y,y=p->labels?HidNpadButton_Y:HidNpadButton_X;
    if(b&a)g.buttons|=0x1000;
    if(b&bb)g.buttons|=0x2000;
    if(b&x)g.buttons|=0x4000;
    if(b&y)g.buttons|=0x8000;
    g.left_trigger=(b&HidNpadButton_ZL)?255:0;g.right_trigger=(b&HidNpadButton_ZR)?255:0;
    HidAnalogStickState l=padGetStickPos(&p->pad,0),r=padGetStickPos(&p->pad,1);
    g.lx=axis(l.x);g.ly=axis(l.y);g.rx=axis(r.x);g.ry=axis(r.y);
    int connected=padIsConnected(&p->pad);uint32_t styles=padGetStyleSet(&p->pad),style=0;
    if(styles&HidNpadStyleTag_NpadHandheld)style=HidNpadStyleTag_NpadHandheld;
    else if(styles&HidNpadStyleTag_NpadFullKey)style=HidNpadStyleTag_NpadFullKey;
    else if(styles&HidNpadStyleTag_NpadJoyDual)style=HidNpadStyleTag_NpadJoyDual;
    uint32_t id=p->pad.active_handheld?HidNpadIdType_Handheld:HidNpadIdType_No1;
    fnv_host_mutex_lock(&p->guard);
    if(p->ready && (!connected || p->style!=style || p->id!=id)){
        HidVibrationValue zero[2]={{0,160,0,320},{0,160,0,320}};
        (void)hidSendVibrationValues(p->vibration,zero,2);
    }
    if(!connected || !style){p->ready=0;p->left=p->right=0;}
    else if(!p->ready || p->style!=style || p->id!=id){
        p->ready=R_SUCCEEDED(hidInitializeVibrationDevices(p->vibration,2,(HidNpadIdType)id,(HidNpadStyleTag)style));
        p->style=style;p->id=id;p->left=p->right=0;
    }
    if(p->ready){HidVibrationValue values[2]={{p->left/65535.0f,160.0f,0,320.0f},{0,160.0f,p->right/65535.0f,320.0f}};
        if(R_FAILED(hidSendVibrationValues(p->vibration,values,2)))p->ready=0;}
    fnv_host_mutex_unlock(&p->guard);return fnv_xinput_update(p->input,0,connected,&g);
}
enum fnv_cpu_result fnv_horizon_pad_close(struct fnv_horizon_pad *p){
    if(!p)return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_xinput_destroy(p->input);if(r!=FNV_CPU_OK)return r;
    if(p->ready){HidVibrationValue zero[2]={{0,160,0,320},{0,160,0,320}};(void)hidSendVibrationValues(p->vibration,zero,2);}
    fnv_host_mutex_destroy(&p->guard);free(p);return FNV_CPU_OK;
}
