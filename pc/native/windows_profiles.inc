/* Read-only INI integers using the existing game-root path validation.
 * No IO while holding process/file/kernel locks, no host registry fallback. */
static int profile_space(unsigned c){return c==' ' || (c>=9 && c<=13);}
static void profile_trim(const char **first,const char **end){
    while(*first<*end && profile_space((unsigned char)**first))(*first)++;
    while(*end>*first && profile_space((unsigned char)(*end)[-1]))(*end)--;
}
static int profile_equal(const char *first,const char *end,const char *name){
    const char *last=name+strlen(name);profile_trim(&first,&end);profile_trim(&name,&last);
    if(end-first!=last-name)return 0;
    while(first<end){unsigned a=(unsigned char)*first++,b=(unsigned char)*name++;
        if(a>='A' && a<='Z')a+=32;
        if(b>='A' && b<='Z')b+=32;
        if(a!=b)return 0;
    }return 1;
}
static uint32_t profile_number(const char *first,const char *end,uint32_t fallback){
    profile_trim(&first,&end);
    if(end-first>=2 && (*first=='\'' || *first=='"') && end[-1]==*first){first++;end--;}
    if(first==end)return fallback;
    while(first<end && profile_space((unsigned char)*first))first++;
    int negative=0;if(first<end && (*first=='-' || *first=='+'))negative=*first++=='-';
    unsigned base=10;
    if(end-first>=2 && first[0]=='0'){
        if(first[1]=='x')base=16;else if(first[1]=='o')base=8;else if(first[1]=='b')base=2;
        if(base!=10)first+=2;
    }
    uint32_t value=0;
    for(;first<end;first++){unsigned c=(unsigned char)*first,digit;
        if(c>='0' && c<='9')digit=c-'0';else if(c>='a' && c<='f')digit=c-'a'+10;
        else if(c>='A' && c<='F')digit=c-'A'+10;else break;
        if(digit>=base)break;
        value=value*base+digit;
    }return negative?0u-value:value;
}
static int profile_parse(const char *data,size_t bytes,const char *section,const char *key,uint32_t fallback,uint32_t *value){
    if((bytes>=2 && (((unsigned char)data[0]==255 && (unsigned char)data[1]==254) || ((unsigned char)data[0]==254 && (unsigned char)data[1]==255))) || memchr(data,0,bytes))return 0;
    int selected=0;*value=fallback;
    for(size_t at=0;at<bytes;){size_t end=at;while(end<bytes && data[end]!='\n' && data[end]!='\r')end++;
        if(end-at>4096)return 0;
        const char *first=data+at,*last=data+end;at=end;
        while(at<bytes && (data[at]=='\n' || data[at]=='\r'))at++;
        profile_trim(&first,&last);if(first==last || *first==';')continue;
        if(*first=='['){const char *close=memchr(first+1,']',(size_t)(last-first-1));
            if(selected)return 1;
            if(close && profile_equal(first+1,close,section))selected=1;
            continue;
        }
        if(selected){const char *equal=memchr(first,'=',(size_t)(last-first));
            if(profile_equal(first,equal?equal:last,key)){
                *value=profile_number(equal?equal+1:last,last,fallback);return 1;
            }
        }
    }return 1;
}
static int profile_value(const char *data,size_t bytes,const char *section,const char *key,const char **first,const char **last,int *found){
    if((bytes>=2 && (((unsigned char)data[0]==255 && (unsigned char)data[1]==254) || ((unsigned char)data[0]==254 && (unsigned char)data[1]==255))) || memchr(data,0,bytes))return 0;
    int selected=0;
    for(size_t at=0;at<bytes;){size_t end=at;while(end<bytes && data[end]!='\n' && data[end]!='\r')end++;
        if(end-at>4096)return 0;
        const char *line=data+at,*line_end=data+end;at=end;
        while(at<bytes && (data[at]=='\n' || data[at]=='\r'))at++;
        profile_trim(&line,&line_end);if(line==line_end || *line==';')continue;
        if(*line=='['){const char *close=memchr(line+1,']',(size_t)(line_end-line-1));
            if(selected)return 1;
            if(close && profile_equal(line+1,close,section))selected=1;
            continue;
        }
        if(selected){const char *equal=memchr(line,'=',(size_t)(line_end-line));
            if(profile_equal(line,equal?equal:line_end,key)){
                if(equal){line=equal+1;profile_trim(&line,&line_end);}else line=line_end;
                if(line_end-line>=2 && (*line=='\'' || *line=='"') && line_end[-1]==*line){line++;line_end--;}
                *first=line;*last=line_end;*found=1;return 1;
            }
        }
    }return 1;
}
static void profile_guest_string(struct fnv_windows_process *p,uint32_t at,int wide,char *out,size_t capacity){
    for(size_t i=0;i<capacity;i++){uint64_t pos=(uint64_t)at+i*(wide?2u:1u);unsigned width=wide?2:1;
        if(pos+width>UINT64_C(0x100000000))sync_invalid("Windows profile string wraps",at);
        unsigned char *b=file_pointer(p,(uint32_t)pos,width,0);unsigned c=b[0]|(wide?((unsigned)b[1]<<8):0);
        if(c>127)sync_invalid("Windows profile query encoding pending",at);
        out[i]=(char)c;if(!c)return;
    }sync_invalid("Windows profile query length pending",at);
}
static void profile_integer(int wide){
    uint32_t section=fnv_cpu_argument_u32(0),key=fnv_cpu_argument_u32(1),fallback=fnv_cpu_argument_u32(2),filename=fnv_cpu_argument_u32(3);
    struct fnv_windows_process *p=file_process();
    if(!section || !key){fnv_windows_set_last_error(0);fnv_cpu_return_u32(0,16);return;}
    if(!filename)sync_invalid("Windows profile default filename pending",filename);
    char app[256],entry[256],path[512],canonical[512],full[1538];
    profile_guest_string(p,section,wide,app,sizeof(app));profile_guest_string(p,key,wide,entry,sizeof(entry));
    int error=file_path(p,filename,wide,path,canonical);
    if(error)sync_invalid("Windows profile integer path unsupported",filename);
#ifdef FNV_PROFILE_TRACE
    fprintf(stderr,"FNV_PROFILE file=%s section=%s key=%s\n",path,app,entry);
#endif
    snprintf(full,sizeof(full),"%s/%s",p->file_root,path);errno=0;FILE *stream=fopen(full,"rb");
    if(!stream){error=(int)fnv_host_file_error(errno);
        if(error!=2 && error!=3)sync_invalid("Windows profile open failed",(uint32_t)error);
        fnv_windows_set_last_error((uint32_t)error);fnv_cpu_return_u32(fallback,16);return;
    }
    uint64_t bytes;
    if(!fnv_host_file_size(stream,&bytes) || bytes>1048576){fclose(stream);sync_invalid("Windows profile file size pending",filename);}
    char *data=malloc((size_t)bytes+1);
    if(!data){fclose(stream);sync_invalid("Windows profile allocation failed",filename);}
    size_t got=fread(data,1,(size_t)bytes,stream);int extra=fgetc(stream),failed=ferror(stream);
    if(fclose(stream))failed=1;
    if(failed || got!=bytes || extra!=EOF){free(data);sync_invalid("Windows profile read failed or changed",filename);}
    uint32_t value;int supported=profile_parse(data,got,app,entry,fallback,&value);free(data);
    if(!supported)sync_invalid("Windows profile file encoding or line length pending",filename);
    fnv_windows_set_last_error(0);fnv_cpu_return_u32(value,16);
}
static void api_profile_integer_a(void){profile_integer(0);}
static void api_profile_integer_w(void){profile_integer(1);}
static void api_profile_string_a(void){
    uint32_t section=fnv_cpu_argument_u32(0),key=fnv_cpu_argument_u32(1),fallback=fnv_cpu_argument_u32(2);
    uint32_t output=fnv_cpu_argument_u32(3),capacity=fnv_cpu_argument_u32(4),filename=fnv_cpu_argument_u32(5);
    struct fnv_windows_process *p=file_process();
    if(!section || !key)sync_invalid("Windows profile string enumeration pending",!section?section:key);
    if(!output || !capacity)sync_invalid("Windows profile string output shape pending",output);
    if(!filename)sync_invalid("Windows profile default filename pending",filename);
    char app[256],entry[256],default_value[512],path[512],canonical[512],full[1538];
    profile_guest_string(p,section,0,app,sizeof(app));profile_guest_string(p,key,0,entry,sizeof(entry));
    if(fallback)profile_guest_string(p,fallback,0,default_value,sizeof(default_value));else default_value[0]=0;
    size_t default_bytes=strlen(default_value);
    while(default_bytes && default_value[default_bytes-1]==' ')default_value[--default_bytes]=0;
    int error=file_path(p,filename,0,path,canonical);
    if(error)sync_invalid("Windows profile string path unsupported",filename);
#ifdef FNV_PROFILE_TRACE
    fprintf(stderr,"FNV_PROFILE file=%s section=%s key=%s\n",path,app,entry);
#endif
    snprintf(full,sizeof(full),"%s/%s",p->file_root,path);errno=0;FILE *stream=fopen(full,"rb");
    if(!stream){error=(int)fnv_host_file_error(errno);
        if(error!=2 && error!=3)sync_invalid("Windows profile open failed",(uint32_t)error);
        const char *value=default_value;size_t n=strlen(value);int truncated=n>=capacity;if(truncated)n=capacity-1;
        unsigned char *dest=file_pointer(p,output,(uint32_t)n+1,1);memcpy(dest,value,n);dest[n]=0;
        fnv_windows_set_last_error((uint32_t)error);
        fnv_cpu_return_u32((uint32_t)n,24);return;
    }
    uint64_t bytes;
    if(!fnv_host_file_size(stream,&bytes) || bytes>1048576){fclose(stream);sync_invalid("Windows profile file size pending",filename);}
    char *data=malloc((size_t)bytes+1);
    if(!data){fclose(stream);sync_invalid("Windows profile allocation failed",filename);}
    size_t got=fread(data,1,(size_t)bytes,stream);int extra=fgetc(stream),failed=ferror(stream);
    if(fclose(stream))failed=1;
    if(failed || got!=bytes || extra!=EOF){free(data);sync_invalid("Windows profile read failed or changed",filename);}
    const char *first=data,*last=data;int found=0;int supported=profile_value(data,got,app,entry,&first,&last,&found);
    if(!supported){free(data);sync_invalid("Windows profile file encoding or line length pending",filename);}
    const char *value=first;size_t n=(size_t)(last-first);
    if(!found){value=default_value;n=strlen(value);}
    int boundary=n>=capacity-1;if(n>=capacity)n=capacity-1;
    unsigned char *dest=file_pointer(p,output,(uint32_t)n+1,1);memcpy(dest,value,n);dest[n]=0;free(data);
    fnv_windows_set_last_error(found?(boundary?234u:0u):2u);fnv_cpu_return_u32((uint32_t)n,24);
}
static const struct fnv_cpu_export profile_exports[]={
    {"kernel32.dll","GetPrivateProfileIntA",0,0,api_profile_integer_a},
    {"kernel32.dll","GetPrivateProfileIntW",0,0,api_profile_integer_w},
    {"kernel32.dll","GetPrivateProfileStringA",0,0,api_profile_string_a}
};
const struct fnv_cpu_export *fnv_windows_profile_exports(size_t *count){if(count)*count=3;return profile_exports;}
