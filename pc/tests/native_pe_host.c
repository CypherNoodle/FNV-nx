#include "pe_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct resolver_state { unsigned calls; size_t total; const char *mode; };
static int resolve_test_token(void *opaque, const struct fnv_native_import *item, uint32_t *token) {
    struct resolver_state *state=opaque;
    (void)item;
    ++state->calls;
    if (!strcmp(state->mode,"missing") && state->calls==state->total) return 0;
    *token=!strcmp(state->mode,"zero")?0:0xf1000000u+state->calls*4;
    return 1;
}
int main(int argc, char **argv) {
    if (argc!=5) return 2;
    size_t capacity=(size_t)strtoul(argv[4],NULL,10);
    if (capacity>128u*1024*1024) return 2;
    unsigned char *data=malloc(capacity+16), *before=NULL;
    struct fnv_native_import *imports=calloc(FNV_NATIVE_MAX_IMPORTS,sizeof(*imports));
    struct fnv_native_image image;
    if (!data || !imports) return 2;
    memset(data,0xa5,capacity+16);
    enum fnv_native_result result=fnv_native_load_image(argv[1],data,capacity,&image);
    size_t count=0; unsigned calls=0; int untouched=1;
    if (result==FNV_NATIVE_OK) {
        before=malloc(image.size);
        if (!before) return 2;
        memcpy(before,data,image.size);
        if (!fnv_native_guest_range(&image,image.base,image.headers) ||
            fnv_native_guest_range(&image,image.base+image.size,1) ||
            fnv_native_guest_range(&image,image.base,((size_t)-1))) return 3;
        for (unsigned i=0; i<image.section_count; ++i) {
            const struct fnv_native_section *s=image.sections+i;
            if (s->size && !fnv_native_guest_range(&image,image.base+s->rva,s->size)) return 3;
        }
        result=fnv_native_collect_imports(&image,imports,FNV_NATIVE_MAX_IMPORTS,&count);
        if (result==FNV_NATIVE_OK && strcmp(argv[3],"collect")) {
            struct resolver_state state={0,count,argv[3]};
            result=fnv_native_bind_imports(&image,imports,
                !strcmp(argv[3],"short")?0:FNV_NATIVE_MAX_IMPORTS,
                resolve_test_token,&state,&count);
            calls=state.calls;
        }
        untouched=!memcmp(before,data,image.size);
        FILE *out=fopen(argv[2],"wb");
        if (!out || fwrite(data,1,image.size,out)!=image.size || fclose(out)) return 2;
    } else if (image.data || fnv_native_guest_range(&image,0,1)) return 3;
    for (size_t i=capacity; i<capacity+16; ++i) if (data[i]!=0xa5) return 3;
    printf("%u %u %u %u %zu %u %d\n",(unsigned)result,image.base,image.size,image.entry_rva,count,calls,untouched);
    for (size_t i=0; i<count; ++i)
        printf("%u\t%u\t%d\t%s\t%s\n",imports[i].iat_rva,imports[i].ordinal,
            imports[i].by_ordinal,imports[i].dll,imports[i].name);
    free(before); free(imports); free(data);
    return 0;
}
