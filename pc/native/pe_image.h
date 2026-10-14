#ifndef FNV_NATIVE_PE_IMAGE_H
#define FNV_NATIVE_PE_IMAGE_H
#include <stddef.h>
#include <stdint.h>

#define FNV_NATIVE_MAX_SECTIONS 96
#define FNV_NATIVE_MAX_IMPORTS 4096
enum fnv_native_result {
    FNV_NATIVE_OK, FNV_NATIVE_IO, FNV_NATIVE_FORMAT,
    FNV_NATIVE_CAPACITY, FNV_NATIVE_UNRESOLVED
};
struct fnv_native_section { uint32_t rva, size, raw_size; };
struct fnv_native_image {
    unsigned char *data;
    uint32_t base, size, entry_rva, headers;
    uint32_t import_rva, import_size, tls_rva, delay_import_rva;
    unsigned section_count;
    struct fnv_native_section sections[FNV_NATIVE_MAX_SECTIONS];
};
struct fnv_native_import {
    uint32_t iat_rva, token;
    uint16_t ordinal;
    int by_ordinal;
    char dll[128], name[256];
};
typedef int (*fnv_native_resolver)(void *context,
    const struct fnv_native_import *import, uint32_t *token);

/* Data-only PE32 loading into caller-owned ordinary memory, at preferred
 * guest base. Does not allocate, create directories, load DLLs or execute x86.
 * Failed loads invalidate image; caller buffer contents may have changed. */
enum fnv_native_result fnv_native_load_image(const char *path,
    void *memory, size_t capacity, struct fnv_native_image *image);
/* Translate a 32-bit guest address to data; rejects holes and wraparound.
 * Never cast a guest address or an IAT token to a host function pointer. */
void *fnv_native_guest_range(const struct fnv_native_image *image,
    uint32_t address, size_t bytes);
/* Collect normal PE imports without modifying image. No dynamic/TLS imports.
 * On failure count describes only the collected prefix, not total imports. */
enum fnv_native_result fnv_native_collect_imports(
    const struct fnv_native_image *image, struct fnv_native_import *imports,
    size_t capacity, size_t *count);
/* Caller must validate executable identity against compiled AOT profile first.
 * Resolve into scratch records, then patch all IAT slots only if every import
 * has a nonzero native token. Unresolved imports leave the entire IAT intact.
 * Resolver must not mutate image or scratch records. Tokens are not Win32 ABI. */
enum fnv_native_result fnv_native_bind_imports(struct fnv_native_image *image,
    struct fnv_native_import *imports, size_t capacity,
    fnv_native_resolver resolver, void *context, size_t *count);
#endif
