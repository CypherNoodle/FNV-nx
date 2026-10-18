/* WIN32_FIND_DATA is marshalled explicitly; no host struct layout passthrough. */
static unsigned file_fold(unsigned c){return c>='A' && c<='Z'?c+32:c;}
static int find_glob(const char *pattern,const char *name){
    const char *star=NULL,*retry=NULL;
    while(*name){if(*pattern=='*'){star=pattern++;retry=name;}
        else if(*pattern && file_fold((unsigned char)*pattern)==file_fold((unsigned char)*name)){pattern++;name++;}
        else if(star){pattern=star+1;name=++retry;}else return 0;}
    while(*pattern=='*')pattern++;
    return !*pattern;
}
static int find_match(const char *pattern,const char *name){
    if(!strcmp(pattern,"*.*"))return 1;
    size_t n=strlen(pattern);char adjusted[260];
    if(n && pattern[n-1]=='.'){
        memcpy(adjusted,pattern,n-1);adjusted[n-1]=0;
        if(strchr(adjusted,'*') && strchr(name,'.'))return 0;
        return find_glob(adjusted,name);
    }
    if(find_glob(pattern,name))return 1;
    if(n>=2 && pattern[n-2]=='.' && pattern[n-1]=='*'){
        memcpy(adjusted,pattern,n-2);adjusted[n-2]=0;return find_glob(adjusted,name);
    }
    return 0;
}
static uint32_t find_ascii(const struct fnv_host_entry *e){
    const char *strings[]={e->name,e->alternate};const size_t sizes[]={sizeof(e->name),sizeof(e->alternate)};
    for(unsigned s=0;s<2;++s){int ended=0;for(size_t i=0;i<sizes[s];++i){unsigned c=(unsigned char)strings[s][i];if(c>127)return 1113;if(!c){ended=1;break;}}if(!ended)return 206;}
    return 0;
}
static int find_entry(struct fnv_host_directory *d,const char *pattern,struct fnv_host_entry *e,uint32_t *error){
    while(fnv_host_directory_next(d,e,error)){
        if(!strcmp(e->name,".") || !strcmp(e->name,".."))continue;
        *error=find_ascii(e);if(*error)return 0;
        if(find_match(pattern,e->name) || (e->alternate[0] && find_match(pattern,e->alternate)))return 1;
    }
    return 0;
}
static void find_write(unsigned char *out,const struct fnv_host_entry *e,int wide){
    memset(out,0,wide?592:320);write32(out,e->attributes);
    uint64_t times[]={e->creation,e->access,e->write};for(unsigned i=0;i<3;++i){write32(out+4+i*8,(uint32_t)times[i]);write32(out+8+i*8,(uint32_t)(times[i]>>32));}
    write32(out+28,(uint32_t)(e->size>>32));write32(out+32,(uint32_t)e->size);write32(out+36,e->reserved[0]);write32(out+40,e->reserved[1]);
    const char *strings[]={e->name,e->alternate};const size_t offsets[]={44,wide?564:304};
    for(unsigned s=0;s<2;++s)for(size_t i=0;strings[s][i];++i)out[offsets[s]+i*(wide?2:1)]=(unsigned char)strings[s][i];
}
static void file_full(struct fnv_windows_process *p,const char *path,char full[1538]){
    size_t n=strlen(p->file_root);memcpy(full,p->file_root,n);full[n++]='/';memcpy(full+n,path,strlen(path)+1);
}
static void file_attributes(int wide){
    struct fnv_windows_process *p=file_process();uint32_t at=fnv_cpu_argument_u32(0),error=0,value=UINT32_MAX;
    char path[512],key[512],full[1538];error=(uint32_t)file_path_mode(p,at,wide,path,key,2);
    if(!error){file_full(p,path,full);struct fnv_host_entry e;error=fnv_host_metadata(full,&e);if(!error)value=e.attributes;
        else if(error==2){char *last=strrchr(full,'/');if(last){*last=0;if(!fnv_host_file_directory(full))error=3;}}}
    fnv_host_mutex_lock(&p->files_guard);p->file_stats.attribute_queries++;fnv_host_mutex_unlock(&p->files_guard);
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,4);
}
static void api_file_attributes_a(void){file_attributes(0);}static void api_file_attributes_w(void){file_attributes(1);}
static void find_first(int wide){
    uint32_t at=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1),error=0,value=UINT32_MAX;
    struct fnv_windows_process *p=file_process();unsigned char *output=file_pointer(p,out,wide?592:320,1);
    char path[512],key[512],full[1538],pattern[260];error=(uint32_t)file_path_mode(p,at,wide,path,key,1);
    struct fnv_host_directory *directory=NULL;struct fnv_host_entry entry;
    if(!error){char *last=strrchr(path,'/');const char *leaf=last?last+1:path;
        size_t n=strlen(leaf);if(n>=sizeof(pattern))error=206;
        else {memcpy(pattern,leaf,n+1);if(last)*last=0;else strcpy(path,".");file_full(p,path,full);
            directory=fnv_host_directory_open(full,&error);
            if(directory && !find_entry(directory,pattern,&entry,&error) && !error)error=2;
        }
    }
    if(!error){fnv_host_mutex_lock(&p->files_guard);
        if(p->file_free==UINT32_MAX)error=4;
        else {uint32_t i=p->file_free;struct file_slot *f=p->files+i;p->file_free=f->next;if(p->file_free==UINT32_MAX)p->file_tail=UINT32_MAX;
            f->generation++;f->kind=2;f->live=1;f->search=directory;f->search_done=0;memcpy(f->pattern,pattern,strlen(pattern)+1);
            value=0xc0000000u|(f->generation<<(p->file_bits+2))|(i<<2);directory=NULL;p->file_stats.live_searches++;p->file_stats.searches++;
        }
        fnv_host_mutex_unlock(&p->files_guard);
    }
    if(directory)fnv_host_directory_close(directory);
    if(!error)find_write(output,&entry,wide);else fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
static void api_find_first_a(void){find_first(0);}static void api_find_first_w(void){find_first(1);}
static void find_next(int wide){
    struct fnv_windows_process *p=file_process();uint32_t h=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1),value=0,error=0;
    unsigned char *output=file_pointer(p,out,wide?592:320,1);struct file_slot *f=file_acquire_kind(p,h,2);struct fnv_host_entry entry;
    if(!f)error=6;
    else {if(f->search_done)error=18;else if(find_entry(f->search,f->pattern,&entry,&error))value=1;else {f->search_done=1;if(!error)error=18;}
        fnv_host_mutex_lock(&p->files_guard);p->file_stats.find_next_calls++;fnv_host_mutex_unlock(&p->files_guard);file_release(p,f);}
    if(value)find_write(output,&entry,wide);else fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
static void api_find_next_a(void){find_next(0);}static void api_find_next_w(void){find_next(1);}
static void api_find_close(void){
    struct fnv_windows_process *p=file_process();uint32_t h=fnv_cpu_argument_u32(0),value=0;int finish=0;
    fnv_host_mutex_lock(&p->files_guard);struct file_slot *f=file_lookup(p,h);
    if(f && f->kind==2){f->live=0;p->file_stats.live_searches--;finish=!f->pins;value=1;}
    fnv_host_mutex_unlock(&p->files_guard);if(finish)file_finish_close(p,f);
    if(!value)fnv_windows_set_last_error(6);
    fnv_cpu_return_u32(value,4);
}
static void api_file_type(void){
    struct fnv_windows_process *p=file_process();uint32_t h=fnv_cpu_argument_u32(0),value=0;struct file_slot *f=file_acquire(p,h);
    if(f){value=1;file_release(p,f);}else fnv_windows_set_last_error(6);
    fnv_cpu_return_u32(value,4);
}
