#ifndef FNV_WINDOWS_COM_DIAG_H
#define FNV_WINDOWS_COM_DIAG_H
#include <stdint.h>
struct fnv_com_activation {
    unsigned char clsid[16],iid[16];
    uint32_t outer,context,result;
};
typedef void (*fnv_com_activation_trace)(void *,const struct fnv_com_activation *);
/* Install/reset only before attach or after all threads detach. Inputs are
 * copied GUID bytes, never host pointers to guest COM objects. Callback must
 * return normally and must not execute guest code. No class providers yet. */
void fnv_windows_com_activation_trace(void *,fnv_com_activation_trace);
#endif
