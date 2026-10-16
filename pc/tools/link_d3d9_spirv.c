/* Offline FNV adapter. Uses an unchanged external MojoShader installation.
 * No game execution, Windows runtime or driver dependency in this tool.
 * attributes.txt: one decimal `usage index element_format` per line, using
 * the public MOJOSHADER_vertexAttribute enums. Outputs must be new files.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mojoshader.h"

static const MOJOSHADER_parseData *read_shader(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
    long n = ftell(f);
    rewind(f);
    if (n < 8 || n > 1024*1024 || n % 4) { fclose(f); return NULL; }
    unsigned char *b = malloc((size_t)n);
    if (!b) { fclose(f); return NULL; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    const MOJOSHADER_parseData *p = NULL;
    if (got == (size_t)n)
        p = MOJOSHADER_parse(MOJOSHADER_PROFILE_SPIRV, "main", b, (unsigned)n,
                            NULL, 0, NULL, 0, NULL, NULL, NULL);
    free(b);
    if (p) for (int i=0; i<p->error_count; ++i)
        fprintf(stderr, "%s:%d: %s\n", path,
                p->errors[i].error_position, p->errors[i].error);
    return p;
}

static int write_pair(const char *vpath, const char *ppath,
                      const MOJOSHADER_parseData *vs, const MOJOSHADER_parseData *ps, int tail) {
    if (tail <= 0 || vs->output_len <= tail + 20 || ps->output_len <= tail + 20 ||
        (vs->output_len-tail)%4 || (ps->output_len-tail)%4) return 0;
    FILE *v = fopen(vpath, "wbx");
    if (!v) return 0;
    FILE *p = fopen(ppath, "wbx");
    if (!p) { fclose(v); remove(vpath); return 0; }
    size_t vn=(size_t)(vs->output_len-tail),pn=(size_t)(ps->output_len-tail);
    int ok=fwrite(vs->output,1,vn,v)==vn;
    ok=(fwrite(ps->output,1,pn,p)==pn) && ok;
    ok=(fclose(v)==0) && ok;
    ok=(fclose(p)==0) && ok;
    if (!ok) { remove(vpath); remove(ppath); }
    return ok;
}

static int audit(const char *manifest, const char *directory) {
    FILE *f=fopen(manifest,"r");if(!f)return 3;
    char hash[128],path[4096];unsigned count=0;
    while(fgets(hash,sizeof(hash),f)){
        hash[strcspn(hash,"\r\n")]=0;
        if(++count>20000 || strlen(hash)!=64 || strspn(hash,"0123456789abcdef")!=64){fclose(f);return 3;}
        int length=snprintf(path,sizeof(path),"%s/%s.bin",directory,hash);
        if(length<0 || (size_t)length>=sizeof(path)){fclose(f);return 3;}
        const MOJOSHADER_parseData *p=read_shader(path);
        if(!p){fclose(f);return 4;}
        printf("{\"hash\":\"%s\",\"translated\":%s,\"error_count\":%d,\"stage\":%d,"
               "\"uniforms\":%d,\"samplers\":%d,\"instructions\":%d}\n",hash,
               p->error_count?"false":"true",p->error_count,p->shader_type,
               p->uniform_count,p->sampler_count,p->instruction_count);
        MOJOSHADER_freeParseData(p);
    }
    int ok=!ferror(f) && count>0;fclose(f);return ok?0:3;
}

static void uniform_metadata(const MOJOSHADER_parseData *p) {
    putchar('[');
    for(int i=0;i<p->uniform_count;++i){
        const MOJOSHADER_uniform *u=&p->uniforms[i];
        if(i)putchar(',');
        printf("{\"kind\":%d,\"first\":%d,\"count\":%d,\"local\":%s}",
            u->type,u->index,u->array_count?u->array_count:1,u->constant?"true":"false");
    }
    putchar(']');
}

int main(int argc, char **argv) {
    if(argc==4 && !strcmp(argv[1],"--audit-manifest"))return audit(argv[2],argv[3]);
    if (argc != 6) {
        fprintf(stderr, "usage: link_d3d9_spirv vs.bin ps.bin attributes.txt vs.spv ps.spv\n");
        return 2;
    }
    MOJOSHADER_vertexAttribute attrs[16];
    int count = 0, usage, index, format;
    char line[128], extra;
    FILE *f = fopen(argv[3], "r");
    if (!f) return 3;
    while (fgets(line, sizeof(line), f)) {
        if (count == 16 || sscanf(line, " %d %d %d %c", &usage, &index, &format, &extra) != 3 ||
            usage < 0 || usage >= MOJOSHADER_USAGE_TOTAL || index < 0 || index >= 16 ||
            format < MOJOSHADER_VERTEXELEMENTFORMAT_SINGLE ||
            format > MOJOSHADER_VERTEXELEMENTFORMAT_HALFVECTOR4) { fclose(f); return 3; }
        for (int j=0; j<count; ++j)
            if (attrs[j].usage == usage && attrs[j].usageIndex == index) { fclose(f); return 3; }
        attrs[count++] = (MOJOSHADER_vertexAttribute){usage, format, index};
    }
    int input_ok = !ferror(f) && count > 0;
    fclose(f);
    if (!input_ok) return 3;
    const MOJOSHADER_parseData *vs = read_shader(argv[1]);
    const MOJOSHADER_parseData *ps = read_shader(argv[2]);
    int result = 4;
    if (!vs || !ps || vs->error_count || ps->error_count ||
        vs->shader_type != MOJOSHADER_TYPE_VERTEX || ps->shader_type != MOJOSHADER_TYPE_PIXEL)
        goto done;
    for (int i=0; i<vs->attribute_count; ++i) {
        int found = 0;
        for (int j=0; j<count; ++j)
            found |= attrs[j].usage == vs->attributes[i].usage &&
                     attrs[j].usageIndex == vs->attributes[i].index;
        if (!found) { fprintf(stderr, "missing vertex input semantic\n"); goto done; }
    }
    /* Builtin pixel inputs and generated legacy varyings need a separate policy.
     * Until implemented, require every pixel input in the VS output reflection.
     */
    for (int i=0; i<ps->attribute_count; ++i) {
        int found = 0;
        for (int j=0; j<vs->output_count; ++j)
            found |= ps->attributes[i].usage == vs->outputs[j].usage &&
                     ps->attributes[i].index == vs->outputs[j].index;
        if (!found) { fprintf(stderr, "missing interstage semantic\n"); goto done; }
    }
    int tail = MOJOSHADER_linkSPIRVShaders(vs, ps, attrs, count);
    if (!write_pair(argv[4], argv[5], vs, ps, tail)) {
        result = 5; goto done;
    }
    printf("{\"patch_table_bytes_removed_per_stage\":%d,\"vertex_bytes\":%d,"
           "\"pixel_bytes\":%d,\"vertex_uniforms\":%d,\"pixel_uniforms\":%d,"
           "\"vertex_samplers\":%d,\"pixel_samplers\":%d,\"vertex_uniform_metadata\":",
           tail, vs->output_len-tail, ps->output_len-tail,
           vs->uniform_count, ps->uniform_count, vs->sampler_count, ps->sampler_count);
    uniform_metadata(vs);printf(",\"pixel_uniform_metadata\":");uniform_metadata(ps);puts("}");
    result = 0;
done:
    MOJOSHADER_freeParseData(vs);
    MOJOSHADER_freeParseData(ps);
    return result;
}
