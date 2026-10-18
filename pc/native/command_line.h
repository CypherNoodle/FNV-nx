#ifndef FNV_COMMAND_LINE_H
#define FNV_COMMAND_LINE_H
#include <stdint.h>
/* Original parser for the CommandLineToArgvW subset, validated with Windows.
 * Input has n UTF-16 code units plus NUL, n <= 32767. No Unicode conversion.
 * Optional output requires n+2 units, offsets n+1 entries. A counting pass
 * with both outputs NULL yields exact sizes. Empty-exe fallback is external. */
static inline void fnv_command_parse(const uint16_t *s,uint32_t n,uint16_t *out,
        uint32_t *offsets,uint32_t *argc,uint32_t *units){
    uint32_t i=0,w=0,count=1;
    if(offsets)offsets[0]=0;
    if(n && s[0]=='"'){
        i++;while(i<n && s[i]!='"'){if(out)out[w]=s[i];w++;i++;}
        if(i<n)i++;
    }else while(i<n && s[i]!=' ' && s[i]!='\t'){if(out)out[w]=s[i];w++;i++;}
    if(out)out[w]=0;
    w++;
    while(i<n){
        while(i<n && (s[i]==' ' || s[i]=='\t'))i++;
        if(i==n)break;
        if(offsets)offsets[count]=w;
        count++;uint32_t quoted=0;
        while(i<n && (quoted || (s[i]!=' ' && s[i]!='\t'))){
            if(s[i]=='\\'){
                uint32_t begin=i;while(i<n && s[i]=='\\')i++;
                uint32_t slashes=i-begin;
                if(i<n && s[i]=='"'){
                    for(uint32_t j=0;j<slashes/2;++j){if(out)out[w]='\\';w++;}
                    if(slashes&1){if(out)out[w]='"';w++;i++;continue;}
                }else{for(uint32_t j=0;j<slashes;++j){if(out)out[w]='\\';w++;}continue;}
            }
            if(i<n && s[i]=='"'){
                do{quoted++;if(quoted==3){if(out)out[w]='"';w++;quoted=0;}i++;}while(i<n && s[i]=='"');
                if(quoted==2)quoted=0;
            }else if(i<n){if(out)out[w]=s[i];w++;i++;}
        }
        if(out)out[w]=0;
        w++;
    }
    *argc=count;*units=w;
}
#endif
