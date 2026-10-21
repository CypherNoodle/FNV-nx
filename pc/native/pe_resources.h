#ifndef FNV_PE_RESOURCES_H
#define FNV_PE_RESOURCES_H
#include "pe_image.h"
struct fnv_pe_resource {const unsigned char *data;uint32_t bytes,codepage;uint16_t language;};
/* Numeric PE32 resources only. Fixed language, then neutral, then smallest
 * numeric language. Borrowed data remains owned by the loaded image. */
enum fnv_resource_result {FNV_RESOURCE_OK,FNV_RESOURCE_ABSENT,FNV_RESOURCE_INVALID};
enum fnv_resource_result fnv_pe_resource_find(const struct fnv_native_image *,uint16_t type,uint16_t id,uint16_t language,struct fnv_pe_resource *);
#endif
