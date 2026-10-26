/* Lexical DOS paths within the game's virtual roots. No host IO or host paths. */
static void full_path_name(int wide){
    struct fnv_windows_process *p=file_process();
    uint32_t input=fnv_cpu_argument_u32(0),capacity=fnv_cpu_argument_u32(1);
    uint32_t output=fnv_cpu_argument_u32(2),part=fnv_cpu_argument_u32(3);
    char path[512],key[512],full[560];int profile=0;
    int error=file_path_mode(p,input,wide,path,key,2,&profile);
    if(error){fnv_windows_set_last_error((uint32_t)error);fnv_cpu_return_u32(0,16);return;}
    const char *tail=path,*root=file_virtual_game_root;
    if(profile){root="C:\\Users\\FNV";tail+=sizeof("UserData/FNV")-1;if(*tail=='/' && tail[1])tail++;}
    else if(!strcmp(tail,"."))tail="";
    else if(!strcmp(tail,"./"))tail="/";
    size_t used=strlen(root);memcpy(full,root,used);
    if(*tail){if(strcmp(tail,"/"))full[used++]='\\';
        while(*tail){full[used++]=*tail=='/'?'\\':*tail;tail++;}}
    full[used]=0;
    if(capacity<=used){fnv_cpu_return_u32((uint32_t)used+1,16);return;}
    unsigned width=wide?2u:1u;uint32_t bytes=((uint32_t)used+1)*width;
    unsigned char *out=file_pointer(p,output,bytes,1),*filepart=NULL;
    if(part){
        if((uint64_t)part< (uint64_t)output+bytes && (uint64_t)output<(uint64_t)part+4)
            sync_invalid("GetFullPathName file-part overlaps output",part);
        filepart=file_pointer(p,part,4,1);
    }
    for(size_t i=0;i<=used;++i){out[i*width]=(unsigned char)full[i];if(wide)out[i*2+1]=0;}
    if(filepart){const char *last=strrchr(full,'\\');uint32_t value=last && last[1]?output+(uint32_t)(last+1-full)*width:0;
        for(unsigned i=0;i<4;++i)filepart[i]=(unsigned char)(value>>(i*8));}
    fnv_cpu_return_u32((uint32_t)used,16);
}
static void api_full_path_a(void){full_path_name(0);}
static void api_full_path_w(void){full_path_name(1);}
