/* Original bounded Win32 string bridge. Virtual ANSI code page is 1252.
 * Full best-fit/composition tables and other code pages are not approximated. */
static unsigned char *charset_pointer(struct fnv_windows_process *p,uint32_t at,uint64_t bytes,int write){
    if(!at || bytes>UINT32_MAX || !inside(&p->config,at,(uint32_t)bytes) ||
       (pool_overlap(p,at,(uint32_t)bytes) && !fnv_vm_access(p->config.memory,at,(uint32_t)bytes,write)))
        sync_invalid("Windows charset pointer unavailable",at);
    return memory(p,at);
}
static uint32_t charset_unit(const unsigned char *input,uint32_t i){return input[i*2]|((uint32_t)input[i*2+1]<<8);}
static int charset_1252(uint32_t value){
    static const uint16_t controls[32]={
        0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
        0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    if(value<128 || (value>=160 && value<=255))return (int)value;
    for(unsigned i=0;i<32;i++)if(value==controls[i])return (int)i+128;
    return -1;
}
static void api_ansi_code_page(void){(void)thread();fnv_cpu_return_u32(1252,0);}
static void api_valid_code_page(void){
    uint32_t cp=fnv_cpu_argument_u32(0);(void)thread();
    fnv_cpu_return_u32(cp==1252 || cp==65001,4);
}
static void api_code_page_info(void){
    uint32_t cp=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1);struct fnv_windows_process *p=thread()->process;
    if(!cp)cp=1252;
    if(!at || (cp!=1252 && cp!=65001)){fnv_windows_set_last_error(87);fnv_cpu_return_u32(0,8);return;}
    /* CPINFO32 has 18 meaningful bytes and two trailing padding bytes.
     * Windows leaves those padding bytes untouched. */
    unsigned char *out=charset_pointer(p,at,18,1);memset(out,0,18);write32(out,cp==1252?1:4);out[4]='?';
    fnv_cpu_return_u32(1,8);
}
static void api_wide_to_multi(void){
    uint32_t cp=fnv_cpu_argument_u32(0),flags=fnv_cpu_argument_u32(1),source=fnv_cpu_argument_u32(2);
    int32_t count=(int32_t)fnv_cpu_argument_u32(3);uint32_t destination=fnv_cpu_argument_u32(4);
    int32_t capacity=(int32_t)fnv_cpu_argument_u32(5);uint32_t replacement=fnv_cpu_argument_u32(6),used_at=fnv_cpu_argument_u32(7);
    struct fnv_windows_process *p=thread()->process;uint32_t error=0,result=0;
    if(!cp)cp=1252;
    if(cp!=1252 && cp!=65001)sync_invalid("Windows charset code page pending",cp);
    if((cp==1252 && flags!=0 && flags!=0x400) || (cp==65001 && flags!=0 && flags!=0x80))
        sync_invalid("Windows charset flags pending",flags);
    if(!source || !count || capacity<0 || (capacity && (!destination || source==destination))){error=87;goto finish;}
    uint32_t units=0;
    if(count<0){
        for(;;){
            uint64_t at=(uint64_t)source+units*2ull;
            if(at>UINT32_MAX)sync_invalid("Windows charset pointer unavailable",source);
            unsigned char *unit=charset_pointer(p,(uint32_t)at,2,0);units++;
            if(!(unit[0]|unit[1]))break;
        }
    }else units=(uint32_t)count;
    unsigned char *input=charset_pointer(p,source,units*2ull,0),*out=NULL,*used=NULL;
    if(capacity){
        out=charset_pointer(p,destination,(uint32_t)capacity,1);
        if(overlap(source,units*2ull,destination,(uint32_t)capacity))sync_invalid("Windows charset overlapping buffers pending",destination);
    }
    unsigned default_byte='?';
    if(replacement && cp==1252)default_byte=*charset_pointer(p,replacement,1,0);
    if(used_at)used=charset_pointer(p,used_at,4,1);
    uint32_t written=0,substituted=0,invalid=0;
    for(uint32_t i=0;i<units;i++){
        uint32_t value=charset_unit(input,i);unsigned char bytes[4];uint32_t n=1,sub=0;
        if(cp==1252){
            int mapped=charset_1252(value);
            if(mapped<0){
                if(!flags)sync_invalid("Windows charset best-fit mapping pending",value);
                bytes[0]=(unsigned char)default_byte;sub=1;
            }else bytes[0]=(unsigned char)mapped;
        }else {
            if(value>=0xd800 && value<=0xdbff && i+1<units){
                uint32_t next=charset_unit(input,i+1);
                if(next>=0xdc00 && next<=0xdfff){value=0x10000+((value-0xd800)<<10)+next-0xdc00;i++;}
            }
            if(value>=0xd800 && value<=0xdfff){value=0xfffd;invalid=1;sub=1;}
            if(value<0x80)bytes[0]=(unsigned char)value;
            else if(value<0x800){n=2;bytes[0]=(unsigned char)(0xc0|(value>>6));bytes[1]=(unsigned char)(0x80|(value&63));}
            else if(value<0x10000){n=3;bytes[0]=(unsigned char)(0xe0|(value>>12));bytes[1]=(unsigned char)(0x80|((value>>6)&63));bytes[2]=(unsigned char)(0x80|(value&63));}
            else {n=4;bytes[0]=(unsigned char)(0xf0|(value>>18));bytes[1]=(unsigned char)(0x80|((value>>12)&63));bytes[2]=(unsigned char)(0x80|((value>>6)&63));bytes[3]=(unsigned char)(0x80|(value&63));}
        }
        if(written>INT32_MAX-n){error=87;break;}
        if(capacity && n>(uint32_t)capacity-written){error=122;break;}
        if(out)memcpy(out+written,bytes,n);
        written+=n;substituted|=sub;
    }
    /* Windows updates the optional UTF-8 substitution flag only after a
     * complete conversion, even when strict mode subsequently reports 1113. */
    if(used && (cp==1252 || error!=122))write32(used,substituted);
    if(!error && invalid && flags==0x80)error=1113;
    if(!error)result=written;
finish:
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(result,32);
}
