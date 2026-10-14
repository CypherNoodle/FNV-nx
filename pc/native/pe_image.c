#include "pe_image.h"
#include <stdio.h>
#include <string.h>

static uint16_t rd16(const unsigned char *p) {
    return (uint16_t)(p[0] | (unsigned)p[1] << 8);
}
static uint32_t rd32(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void wr32(unsigned char *p, uint32_t value) {
    for (unsigned i=0; i<4; ++i) p[i]=(unsigned char)(value >> (8*i));
}
static int read_at(FILE *file, uint64_t file_size, uint32_t at, void *data, size_t size) {
    return at<=file_size && size<=file_size-at && !fseek(file,(long)at,SEEK_SET)
        && fread(data,1,size,file)==size;
}
static int within(uint32_t at, size_t size, uint32_t start, uint32_t length) {
    return at>=start && (uint64_t)at-start<=length && size<=length-(uint64_t)(at-start);
}
static unsigned char *rva_range(const struct fnv_native_image *image,
    uint32_t rva, size_t size, int raw_only) {
    if (!image || !image->data || rva>image->size || size>image->size-rva) return NULL;
    if (within(rva,size,0,image->headers)) return image->data+rva;
    for (unsigned i=0; i<image->section_count; ++i) {
        const struct fnv_native_section *s=image->sections+i;
        if (within(rva,size,s->rva,raw_only?s->raw_size:s->size)) return image->data+rva;
    }
    return NULL;
}
void *fnv_native_guest_range(const struct fnv_native_image *image,
    uint32_t address, size_t size) {
    if (!image || address<image->base) return NULL;
    return rva_range(image,address-image->base,size,0);
}
enum fnv_native_result fnv_native_load_image(const char *path,
    void *memory, size_t capacity, struct fnv_native_image *image) {
    unsigned char header[248], section[40];
    uint32_t offsets[FNV_NATIVE_MAX_SECTIONS], nt, optional, table;
    enum fnv_native_result result=FNV_NATIVE_FORMAT;
    FILE *file=NULL;
    long file_bytes;
    if (!image) return FNV_NATIVE_FORMAT;
    memset(image,0,sizeof(*image));
    if (!path || !memory) return FNV_NATIVE_FORMAT;
    file=fopen(path,"rb");
    if (!file) return FNV_NATIVE_IO;
    if (fseek(file,0,SEEK_END) || (file_bytes=ftell(file))<0) {
        result=FNV_NATIVE_IO; goto done;
    }
    if (file_bytes>128*1024*1024 || !read_at(file,file_bytes,0,header,64) ||
        header[0]!='M' || header[1]!='Z') goto done;
    nt=rd32(header+60);
    if (!read_at(file,file_bytes,nt,header,24) || memcmp(header,"PE\0\0",4) ||
        rd16(header+4)!=0x14c || (rd16(header+22)&0x2000)) goto done;
    image->section_count=rd16(header+6);
    unsigned optional_size=rd16(header+20);
    if (!image->section_count || image->section_count>FNV_NATIVE_MAX_SECTIONS ||
        optional_size<224 || (uint64_t)nt+24+optional_size>UINT32_MAX) goto done;
    optional=nt+24; table=optional+optional_size;
    if (!read_at(file,file_bytes,optional,header,224) || rd16(header)!=0x10b ||
        rd32(header+92)<16) goto done;
    image->base=rd32(header+28); image->size=rd32(header+56);
    image->entry_rva=rd32(header+16); image->headers=rd32(header+60);
    image->import_rva=rd32(header+104); image->import_size=rd32(header+108);
    image->tls_rva=rd32(header+168); image->delay_import_rva=rd32(header+200);
    if (!image->size || image->size>128*1024*1024 ||
        (uint64_t)image->base+image->size>UINT64_C(0x100000000) ||
        !image->headers || image->headers>image->size || image->headers>(uint64_t)file_bytes ||
        (uint64_t)table+image->section_count*40>image->headers ||
        image->entry_rva>=image->size) goto done;
    if (image->size>capacity) { result=FNV_NATIVE_CAPACITY; goto done; }
    for (unsigned i=0; i<image->section_count; ++i) {
        struct fnv_native_section *s=image->sections+i;
        if (!read_at(file,file_bytes,table+i*40,section,40)) goto done;
        s->size=rd32(section+8); s->rva=rd32(section+12);
        s->raw_size=rd32(section+16); offsets[i]=rd32(section+20);
        if (s->size<s->raw_size) s->size=s->raw_size;
        if (s->rva>image->size || s->size>image->size-s->rva ||
            (s->size && s->rva<image->headers) ||
            (s->raw_size && ((uint64_t)offsets[i]+s->raw_size>(uint64_t)file_bytes ||
                offsets[i]<image->headers))) goto done;
        for (unsigned j=0; j<i; ++j) {
            struct fnv_native_section *p=image->sections+j;
            if (s->size && p->size && s->rva<(uint64_t)p->rva+p->size &&
                p->rva<(uint64_t)s->rva+s->size) goto done;
        }
    }
    image->data=memory;
    if (!rva_range(image,image->entry_rva,1,0) ||
        (!!image->import_rva != !!image->import_size) ||
        (image->import_rva && !rva_range(image,image->import_rva,image->import_size,1))) goto done;
    memset(memory,0,image->size);
    if (!read_at(file,file_bytes,0,memory,image->headers)) { result=FNV_NATIVE_IO; goto done; }
    for (unsigned i=0; i<image->section_count; ++i) {
        struct fnv_native_section *s=image->sections+i;
        if (s->raw_size && !read_at(file,file_bytes,offsets[i],image->data+s->rva,s->raw_size)) {
            result=FNV_NATIVE_IO; goto done;
        }
    }
    result=FNV_NATIVE_OK;
done:
    fclose(file);
    if (result!=FNV_NATIVE_OK) memset(image,0,sizeof(*image));
    return result;
}
static int string_at(const struct fnv_native_image *image, uint32_t rva,
    char *out, size_t capacity, int dll) {
    for (size_t i=0; i<capacity; ++i) {
        if ((uint64_t)rva+i>UINT32_MAX) return 0;
        const unsigned char *p=rva_range(image,rva+(uint32_t)i,1,1);
        if (!p) return 0;
        unsigned c=*p;
        if (!c) { out[i]=0; return i>0 && (!dll || !strstr(out,"..")); }
        if (c<33 || c>126 || (dll && !((c>='a'&&c<='z') ||
            (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-' || c=='.'))) return 0;
        out[i]=(char)(dll && c>='A'&&c<='Z' ? c+32 : c);
    }
    return 0;
}
enum fnv_native_result fnv_native_collect_imports(const struct fnv_native_image *image,
    struct fnv_native_import *imports, size_t capacity, size_t *count) {
    if (!count) return FNV_NATIVE_FORMAT;
    *count=0;
    if (!image || !image->data || (!imports && capacity)) return FNV_NATIVE_FORMAT;
    if (!image->import_rva) return FNV_NATIVE_OK;
    if (image->import_size<20 || image->import_size/20>129) return FNV_NATIVE_FORMAT;
    for (unsigned d=0; d<image->import_size/20; ++d) {
        uint64_t desc_rva=(uint64_t)image->import_rva+d*20;
        if (desc_rva>UINT32_MAX) return FNV_NATIVE_FORMAT;
        const unsigned char *desc=rva_range(image,(uint32_t)desc_rva,20,1);
        if (!desc) return FNV_NATIVE_FORMAT;
        uint32_t fields[5]; unsigned nonzero=0;
        for (unsigned i=0; i<5; ++i) { fields[i]=rd32(desc+4*i); nonzero|=fields[i]; }
        if (!nonzero) return FNV_NATIVE_OK;
        uint32_t lookup=fields[0]?fields[0]:fields[4], iat=fields[4];
        char dll[128]={0};
        if (!lookup || !iat || (lookup&3) || (iat&3) ||
            !string_at(image,fields[3],dll,sizeof(dll),1)) return FNV_NATIVE_FORMAT;
        unsigned j;
        for (j=0; j<FNV_NATIVE_MAX_IMPORTS; ++j) {
            uint64_t lookup_rva=(uint64_t)lookup+j*4, iat_rva=(uint64_t)iat+j*4;
            if (lookup_rva>UINT32_MAX || iat_rva>UINT32_MAX) return FNV_NATIVE_FORMAT;
            const unsigned char *value_ptr=rva_range(image,(uint32_t)lookup_rva,4,1);
            if (!value_ptr || !rva_range(image,(uint32_t)iat_rva,4,1)) return FNV_NATIVE_FORMAT;
            uint32_t value=rd32(value_ptr);
            if (!value) break;
            if (*count>=capacity || *count>=FNV_NATIVE_MAX_IMPORTS) return FNV_NATIVE_CAPACITY;
            struct fnv_native_import *item=imports+*count;
            memset(item,0,sizeof(*item)); item->iat_rva=(uint32_t)iat_rva;
            memcpy(item->dll,dll,sizeof(dll));
            item->by_ordinal=!!(value&0x80000000u);
            if (item->by_ordinal) {
                if (value&0x7fff0000u) return FNV_NATIVE_FORMAT;
                item->ordinal=(uint16_t)value;
            } else if ((uint64_t)value+2>UINT32_MAX || !rva_range(image,value,2,1) ||
                !string_at(image,value+2,item->name,sizeof(item->name),0)) return FNV_NATIVE_FORMAT;
            for (size_t k=0; k<*count; ++k)
                if (imports[k].iat_rva==item->iat_rva) return FNV_NATIVE_FORMAT;
            ++*count;
        }
        if (j==FNV_NATIVE_MAX_IMPORTS) return FNV_NATIVE_FORMAT;
    }
    return FNV_NATIVE_FORMAT;
}
enum fnv_native_result fnv_native_bind_imports(struct fnv_native_image *image,
    struct fnv_native_import *imports, size_t capacity,
    fnv_native_resolver resolver, void *context, size_t *count) {
    enum fnv_native_result result=fnv_native_collect_imports(image,imports,capacity,count);
    if (result!=FNV_NATIVE_OK) return result;
    if (!resolver && *count) return FNV_NATIVE_UNRESOLVED;
    for (size_t i=0; i<*count; ++i) {
        uint32_t token=0;
        if (!resolver(context,imports+i,&token) || !token) return FNV_NATIVE_UNRESOLVED;
        imports[i].token=token;
    }
    for (size_t i=0; i<*count; ++i) wr32(image->data+imports[i].iat_rva,imports[i].token);
    return FNV_NATIVE_OK;
}
