#include "dinput8_hid.h"
#include <string.h>
unsigned fnv_di_usb_keyboard(const uint64_t keys[4],uint64_t modifiers,struct fnv_di_snapshot *out){
    static const unsigned char scan[256]={
        [4]=0x1e,[5]=0x30,[6]=0x2e,[7]=0x20,[8]=0x12,[9]=0x21,[10]=0x22,[11]=0x23,[12]=0x17,[13]=0x24,[14]=0x25,[15]=0x26,
        [16]=0x32,[17]=0x31,[18]=0x18,[19]=0x19,[20]=0x10,[21]=0x13,[22]=0x1f,[23]=0x14,[24]=0x16,[25]=0x2f,[26]=0x11,[27]=0x2d,[28]=0x15,[29]=0x2c,
        [30]=2,[31]=3,[32]=4,[33]=5,[34]=6,[35]=7,[36]=8,[37]=9,[38]=10,[39]=11,
        [40]=0x1c,[41]=1,[42]=0x0e,[43]=0x0f,[44]=0x39,[45]=0x0c,[46]=0x0d,[47]=0x1a,[48]=0x1b,[49]=0x2b,
        [51]=0x27,[52]=0x28,[53]=0x29,[54]=0x33,[55]=0x34,[56]=0x35,[57]=0x3a,
        [58]=0x3b,[59]=0x3c,[60]=0x3d,[61]=0x3e,[62]=0x3f,[63]=0x40,[64]=0x41,[65]=0x42,[66]=0x43,[67]=0x44,[68]=0x57,[69]=0x58,
        [70]=0xb7,[71]=0x46,[72]=0xc5,[73]=0xd2,[74]=0xc7,[75]=0xc9,[76]=0xd3,[77]=0xcf,[78]=0xd1,[79]=0xcd,[80]=0xcb,[81]=0xd0,[82]=0xc8,
        [83]=0x45,[84]=0xb5,[85]=0x37,[86]=0x4a,[87]=0x4e,[88]=0x9c,[89]=0x4f,[90]=0x50,[91]=0x51,[92]=0x4b,[93]=0x4c,[94]=0x4d,[95]=0x47,[96]=0x48,[97]=0x49,[98]=0x52,[99]=0x53,[100]=0x56,[101]=0xdd,
        [224]=0x1d,[225]=0x2a,[226]=0x38,[227]=0xdb,[228]=0x9d,[229]=0x36,[230]=0xb8,[231]=0xdc};
    memset(out->keys,0,sizeof(out->keys));unsigned unknown=0;
    for(unsigned i=0;i<256;i++)if(keys[i/64]&(UINT64_C(1)<<(i%64))){if(scan[i])out->keys[scan[i]]=128;else unknown++;}
    if((modifiers&1) && !out->keys[0x1d] && !out->keys[0x9d])out->keys[0x1d]=128;
    if((modifiers&2) && !out->keys[0x2a] && !out->keys[0x36])out->keys[0x2a]=128;
    if(modifiers&4)out->keys[0x38]=128;
    if(modifiers&8)out->keys[0xb8]=128;
    if((modifiers&16) && !out->keys[0xdb] && !out->keys[0xdc])out->keys[0xdb]=128;
    return unknown;
}
static int32_t bounded_delta(int64_t v){return v>INT32_MAX?INT32_MAX:v<INT32_MIN?INT32_MIN:(int32_t)v;}
int fnv_di_collect_mouse(const struct fnv_di_mouse_sample *samples,unsigned n,struct fnv_di_horizon_poll *poll,struct fnv_di_snapshot *out){
    if(!poll || !out || n>17 || (n && !samples))return 0;
    for(unsigned i=1;i<n;i++)if(samples[i].sample>=samples[i-1].sample)return 0;
    out->dx=out->dy=out->wheel=0;memset(out->buttons,0,8);if(!n)return 1;
    static const unsigned char buttons[]={0,1,2,4,3};
    for(unsigned i=0;i<5;i++)if(samples[0].buttons&(1u<<i))out->buttons[buttons[i]]=128;
    int64_t dx=0,dy=0,wheel=0;unsigned observed=0;
    for(unsigned i=0;i<n;i++)if((poll->mouse_ready?samples[i].sample>poll->mouse_sample:i==0)){
        dx+=samples[i].dx;dy+=samples[i].dy;wheel+=samples[i].wheel;observed++;}
    if(poll->mouse_ready && samples[0].sample>poll->mouse_sample && samples[0].sample-poll->mouse_sample>observed){
        uint64_t missed=samples[0].sample-poll->mouse_sample-observed;
        poll->missed_mouse_samples=UINT64_MAX-poll->missed_mouse_samples<missed?UINT64_MAX:poll->missed_mouse_samples+missed;}
    if(!poll->mouse_ready || samples[0].sample>poll->mouse_sample)poll->mouse_sample=samples[0].sample;
    poll->mouse_ready=1;
    out->dx=bounded_delta(dx);out->dy=bounded_delta(dy);out->wheel=bounded_delta(wheel);return 1;
}
#ifdef __SWITCH__
#include <switch.h>
enum fnv_cpu_result fnv_di_horizon_poll(struct fnv_dinput8 *input,struct fnv_di_horizon_poll *poll,unsigned *unmapped){
    if(!input || !poll)return FNV_CPU_INVALID;
    HidKeyboardState keyboard={0};HidMouseState mouse[17];struct fnv_di_snapshot state={0};
    size_t nk=hidGetKeyboardStates(&keyboard,1),nm=hidGetMouseStates(mouse,17);
    struct fnv_di_mouse_sample samples[17];
    for(size_t i=0;i<nm;i++)samples[i]=(struct fnv_di_mouse_sample){mouse[i].sampling_number,mouse[i].delta_x,mouse[i].delta_y,mouse[i].wheel_delta_y,mouse[i].buttons};
    unsigned unknown=nk?fnv_di_usb_keyboard(keyboard.keys,keyboard.modifiers,&state):0;
    struct fnv_di_horizon_poll next=*poll;
    if(!fnv_di_collect_mouse(samples,(unsigned)nm,&next,&state))return FNV_CPU_INVALID;
    enum fnv_cpu_result r=fnv_dinput8_update(input,&state);
    if(r==FNV_CPU_OK){if(nk)next.keyboard_sample=keyboard.sampling_number;*poll=next;if(unmapped)*unmapped=unknown;}
    return r;
}
#endif
