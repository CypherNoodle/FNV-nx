#ifndef FNV_XINPUT_H
#define FNV_XINPUT_H
#include "windows_thread.h"
struct fnv_xinput;
/* XInput button bits, not HID bits. Raw axes, no extra deadzone. */
struct fnv_gamepad {uint16_t buttons;uint8_t left_trigger,right_trigger;int16_t lx,ly,rx,ry;};
struct fnv_xinput_config {void *context;uint32_t (*rumble)(void *,uint32_t,uint16_t,uint16_t);};
/* Setup/teardown serialized by host; destroy after all guest threads detach.
 * Update may run concurrently with guest reads. Callback lifetime covers provider.
 * Callback must serialize its own device access; no guest callback/reentry. */
enum fnv_cpu_result fnv_xinput_create(struct fnv_windows_process *,const struct fnv_xinput_config *,struct fnv_xinput **);
enum fnv_cpu_result fnv_xinput_destroy(struct fnv_xinput *);
enum fnv_cpu_result fnv_xinput_update(struct fnv_xinput *,unsigned user,int connected,const struct fnv_gamepad *);
const struct fnv_cpu_export *fnv_xinput_exports(size_t *count);
#endif
