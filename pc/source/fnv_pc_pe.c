#include "fnv_pc_pe.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct section { uint32_t rva, bytes, offset; };
struct reader { FILE *f; uint64_t size; uint32_t headers; unsigned n; struct section sections[96]; };
static int read_at(struct reader *r, uint64_t at, void *out, size_t n) {
    if (at>r->size || n>r->size-at || fseek(r->f,(long)at,SEEK_SET)) return 0;
    return fread(out,1,n,r->f)==n;
}
static unsigned u16(const unsigned char *b) { return b[0] | (unsigned)b[1]<<8; }
static uint32_t u32(const unsigned char *b) { return b[0] | (uint32_t)b[1]<<8 | (uint32_t)b[2]<<16 | (uint32_t)b[3]<<24; }
static int read_rva(struct reader *r, uint32_t va, void *out, size_t n) {
    if (va<r->headers && n<=r->headers-va) return read_at(r,va,out,n);
    for (unsigned i=0;i<r->n;++i) {
        struct section *s=r->sections+i;
        if (va>=s->rva && (uint64_t)va-s->rva+n<=s->bytes)
            return read_at(r,(uint64_t)s->offset+va-s->rva,out,n);
    }
    return 0;
}
int fnv_pc_read_pe(const char *path, struct fnv_pc_pe *info, FILE *log) {
    struct reader r={0}; unsigned char b[256]; uint32_t nt, optional, table, import_rva, import_size;
    unsigned optional_size; long bytes; int entry_found=0, ok=0;
    memset(info,0,sizeof(*info));
    r.f=fopen(path,"rb"); if (!r.f) goto done;
    if (fseek(r.f,0,SEEK_END) || (bytes=ftell(r.f))<0 || bytes>128*1024*1024) goto done;
    r.size=(uint64_t)bytes;
    if (!read_at(&r,0,b,64) || b[0]!='M' || b[1]!='Z') goto done;
    nt=u32(b+60);
    if (!read_at(&r,nt,b,24) || memcmp(b,"PE\0\0",4) || u16(b+4)!=0x14c) goto done;
    info->file_characteristics=u16(b+22);
    r.n=u16(b+6); optional_size=u16(b+20);
    if (!r.n || r.n>96 || optional_size<112 || nt>UINT32_MAX-24-optional_size) goto done;
    optional=nt+24; table=optional+optional_size;
    if (!read_at(&r,optional,b,112) || u16(b)!=0x10b) goto done;
    info->entry_rva=u32(b+16); info->image_base=u32(b+28); info->image_bytes=u32(b+56);
    r.headers=u32(b+60);
    if (!r.headers || r.headers>r.size || info->entry_rva>=info->image_bytes ||
        (uint64_t)info->image_base+info->image_bytes>UINT64_C(0x100000000)) goto done;
    import_rva=u32(b+92)>1 ? u32(b+104):0; import_size=u32(b+108);
    for (unsigned i=0;i<r.n;++i) {
        struct section *s=r.sections+i; uint32_t virtual_bytes;
        if (!read_at(&r,(uint64_t)table+i*40,b,40)) goto done;
        virtual_bytes=u32(b+8); s->rva=u32(b+12); s->bytes=u32(b+16); s->offset=u32(b+20);
        if ((uint64_t)s->offset+s->bytes>r.size || (uint64_t)s->rva+virtual_bytes>info->image_bytes) goto done;
        if (info->entry_rva>=s->rva && (uint64_t)info->entry_rva-s->rva<virtual_bytes) {
            entry_found=1; info->entry_in_bind=!memcmp(b,".bind\0",6);
        }
    }
    if (!entry_found) goto done;
    if (import_rva) {
        unsigned limit=import_size/20;
        if (!limit || limit>FNV_PC_MAX_IMPORTS+1) goto done;
        for (unsigned i=0;i<limit;++i) {
            uint32_t name; char *out;
            if ((uint64_t)import_rva+i*20>UINT32_MAX || !read_rva(&r,import_rva+i*20,b,20)) goto done;
            if (!(u32(b)|u32(b+4)|u32(b+8)|u32(b+12)|u32(b+16))) { ok=1; break; }
            if (info->import_count>=FNV_PC_MAX_IMPORTS) goto done;
            name=u32(b+12); out=info->imports[info->import_count];
            for (unsigned j=0;j<128;++j) {
                if ((uint64_t)name+j>UINT32_MAX || !read_rva(&r,name+j,b,1)) goto done;
                if (!b[0]) { out[j]=0; break; }
                if (!(isalnum(b[0]) || b[0]=='_' || b[0]=='-' || b[0]=='.') || j==127) goto done;
                out[j]=(char)tolower(b[0]);
            }
            if (!out[0] || strstr(out,"..")) goto done;
            info->steam_api |= !strcmp(out,"steam_api.dll") || !strcmp(out,"steam_api64.dll");
            info->galaxy_wrapper |= !strcmp(out,"galaxywrp.dll") || !strcmp(out,"galaxy.dll");
            ++info->import_count;
        }
    } else ok=1;
done:
    if (r.f) fclose(r.f);
    if (!ok && log) fprintf(log,"pe_invalid=%s; expected bounded i386 PE32 headers/imports\n",path);
    return ok;
}
