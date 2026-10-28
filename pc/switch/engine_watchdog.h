#ifndef FNV_ENGINE_WATCHDOG_H
#define FNV_ENGINE_WATCHDOG_H
#include <stdint.h>
/* Dedicated observer owns its FILE and never enters guest/provider locks. */
int fnv_engine_watchdog_start(uint32_t monitor_handle,uint32_t guest_handle);
void fnv_engine_watchdog_stage(unsigned stage);
/* Distinct stages around host calls, not guest CPU instruction addresses. */
enum {FNV_MON_START,FNV_MON_APPLET,FNV_MON_PAD,FNV_MON_KEYBOARD,FNV_MON_MOUSE,
      FNV_MON_DINPUT,FNV_MON_USER32,FNV_MON_SOUND,FNV_MON_CONTROLLER,
      FNV_MON_DISPLAY,FNV_MON_STOP,FNV_MON_STATS,FNV_MON_SLEEP};
#endif
