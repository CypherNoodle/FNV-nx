#ifndef FNV_WINDOWS_COM_CLASSES_H
#define FNV_WINDOWS_COM_CLASSES_H
#include <stdint.h>
/* Returns nonzero only for a class owned by this provider. On success returns
 * a real guest COM interface with one reference. Lifecycle: before attach / after
 * detach, exactly one provider. No host COM or arbitrary DLL activation. */
typedef int (*fnv_com_class_factory)(void *,const unsigned char[16],uint32_t,uint32_t,const unsigned char[16],uint32_t *,uint32_t *);
int fnv_windows_com_set_factory(void *,fnv_com_class_factory);
#endif
