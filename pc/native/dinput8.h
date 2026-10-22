#ifndef FNV_DINPUT8_H
#define FNV_DINPUT8_H
#include "windows_thread.h"
struct fnv_dinput8;
struct fnv_di_snapshot {unsigned char keys[256],buttons[8];int32_t dx,dy,wheel;};
/* A configured virtual keyboard/mouse has actual snapshot storage. Host updates
 * once per input poll; GetState never polls HID or synthesizes a fake device.
 * Lifetime setup before attach, destruction after detach. Objects never reused.
 * objects_address owns >=4096 RW bytes disjoint from guest-owned arenas. */
struct fnv_dinput8_config {
    uint32_t instance,objects_address,objects_bytes;int keyboard,mouse;
    struct fnv_guest_memory *memory;
    void *context;int (*valid_window)(void *,uint32_t);
};
enum fnv_cpu_result fnv_dinput8_create(struct fnv_windows_process *,const struct fnv_dinput8_config *,struct fnv_dinput8 **);
enum fnv_cpu_result fnv_dinput8_destroy(struct fnv_dinput8 *);
enum fnv_cpu_result fnv_dinput8_update(struct fnv_dinput8 *,const struct fnv_di_snapshot *);
const struct fnv_cpu_export *fnv_dinput8_exports(size_t *);
#endif
