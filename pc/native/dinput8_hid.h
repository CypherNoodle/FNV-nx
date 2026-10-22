#ifndef FNV_DINPUT8_HID_H
#define FNV_DINPUT8_HID_H
#include "dinput8.h"
/* USB usage -> DIK conversion. Writes keys only; returns pressed usages not
 * represented by the bounded mapping (for host diagnostics). No lock toggles
 * are treated as physical presses. Other snapshot fields remain unchanged. */
unsigned fnv_di_usb_keyboard(const uint64_t keys[4],uint64_t modifiers,struct fnv_di_snapshot *);
struct fnv_di_horizon_poll {uint64_t keyboard_sample,mouse_sample,missed_mouse_samples;int mouse_ready;};
/* buttons uses libnx HID order: left,right,middle,forward,back. */
struct fnv_di_mouse_sample {uint64_t sample;int32_t dx,dy,wheel;uint32_t buttons;};
/* Newest-first retained samples. Accumulates only new relative motion; initial
 * poll consumes latest sample only. Returns zero for malformed ordering/count. */
int fnv_di_collect_mouse(const struct fnv_di_mouse_sample *,unsigned,struct fnv_di_horizon_poll *,struct fnv_di_snapshot *);
#ifdef __SWITCH__
/* App initializes HID keyboard/mouse, zeroes poll, and calls on one input
 * thread. Takes only new
 * mouse samples so repeated polls do not replay relative motion. */
enum fnv_cpu_result fnv_di_horizon_poll(struct fnv_dinput8 *,struct fnv_di_horizon_poll *,unsigned *unmapped_keys);
#endif
#endif
