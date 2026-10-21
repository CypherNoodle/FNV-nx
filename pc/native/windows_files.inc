/* Original synchronous, read-only guest file ABI. Separate table and per-file
 * IO locks: no SD operation holds kernel_guard, process guard, or files_guard. */
#include "host_file.h"
#include "host_directory.h"
#include <string.h>
struct file_slot {
    FILE *stream;struct fnv_host_directory *search;fnv_host_mutex io_guard;
    uint32_t generation,live,pins,next,share,kind,search_done;
    char key[512];
    char pattern[260];
};
static enum fnv_cpu_result files_prepare(struct fnv_windows_process *p){
    uint32_t count=p->config.file_handle_capacity;
    if(!count)return p->config.file_root || p->config.file_generation_limit?FNV_CPU_INVALID:FNV_CPU_OK;
    if(count>4096 || !p->config.file_root)return FNV_CPU_INVALID;
    size_t n=0;while(n<1024 && p->config.file_root[n])n++;
    if(!n || n==1024 || !fnv_host_file_directory(p->config.file_root))return FNV_CPU_INVALID;
    uint32_t bits=0;while((1u<<bits)<count)bits++;
    uint32_t maximum=(1u<<(26-bits))-1;
    if(p->config.file_generation_limit>maximum)return FNV_CPU_INVALID;
    char *root=malloc(n+1);struct file_slot *items=calloc(count,sizeof(*items));
    if(!root || !items){free(root);free(items);return FNV_CPU_NO_MEMORY;}
    if(!fnv_host_mutex_init(&p->files_guard)){free(root);free(items);return FNV_CPU_NO_MEMORY;}
    for(uint32_t i=0;i<count;++i){
        if(!fnv_host_mutex_init(&items[i].io_guard)){
            while(i)fnv_host_mutex_destroy(&items[--i].io_guard);
            fnv_host_mutex_destroy(&p->files_guard);free(root);free(items);return FNV_CPU_NO_MEMORY;
        }
        items[i].next=i+1<count?i+1:UINT32_MAX;
    }
    memcpy(root,p->config.file_root,n+1);p->file_root=root;p->files=items;p->file_bits=bits;p->file_mask=(1u<<bits)-1;
    p->file_generation_max=p->config.file_generation_limit?p->config.file_generation_limit:maximum;
    p->file_free=0;p->file_tail=count-1;return FNV_CPU_OK;
}
static void files_dispose(struct fnv_windows_process *p){
    if(!p->files)return;
    for(uint32_t i=0;i<p->config.file_handle_capacity;++i){if(p->files[i].stream)fclose(p->files[i].stream);
        if(p->files[i].search)fnv_host_directory_close(p->files[i].search);
        fnv_host_mutex_destroy(&p->files[i].io_guard);}
    free(p->files);free(p->file_root);fnv_host_mutex_destroy(&p->files_guard);
}
static struct fnv_windows_process *file_process(void){
    struct fnv_windows_process *p=thread()->process;
    if(!p->files)sync_invalid("Windows file provider unavailable",0);
    return p;
}
static struct file_slot *file_lookup(struct fnv_windows_process *p,uint32_t h){
    uint32_t prefix=h&0xf0000000u;h&=~3u;if(!p->files || (prefix!=0xd0000000u && prefix!=0xc0000000u))return NULL;
    uint32_t i=(h>>2)&p->file_mask,g=(h&0x0fffffffu)>>(p->file_bits+2);
    if(i>=p->config.file_handle_capacity || !g)return NULL;
    struct file_slot *f=p->files+i;return f->live && f->generation==g && ((prefix==0xd0000000u && f->kind==1) || (prefix==0xc0000000u && f->kind==2))?f:NULL;
}
static unsigned char *file_pointer(struct fnv_windows_process *p,uint32_t at,uint32_t bytes,int write){
    if(!bytes)return NULL;
    if(!at || !inside(&p->config,at,bytes) || (pool_overlap(p,at,bytes) && !fnv_vm_access(p->config.memory,at,bytes,write)))
        sync_invalid("Windows file argument unreadable or not writable",at);
    return memory(p,at);
}
static size_t file_virtual_profile_prefix(const char *source){
    static const char prefix[]="C:\\Users\\FNV\\";
    for(size_t i=0;i<sizeof(prefix)-1;++i){unsigned a=(unsigned char)source[i],b=(unsigned char)prefix[i];
        if(!a)return 0;
        if(a=='/')a='\\';
        if(b>='A' && b<='Z')b+=32;
        if(a>='A' && a<='Z')a+=32;
        if(a!=b)return 0;
    }
    return sizeof(prefix)-1;
}
static int file_path_mode(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key,int mode){
    char source[512];int ended=0;
    for(size_t i=0;i<sizeof(source);++i){uint64_t pos=(uint64_t)at+i*(wide?2:1);unsigned width=wide?2:1;
        if(pos+width>UINT64_C(0x100000000))sync_invalid("Windows file path wraps",at);
        unsigned char *b=file_pointer(p,(uint32_t)pos,width,0);unsigned c=b[0]|(wide?((unsigned)b[1]<<8):0);
        if(c>127)return 123;
        source[i]=(char)c;if(!c){ended=1;break;}
    }
    if(!ended || !source[0])return 123;
    size_t profile_floor=0,prefix=file_virtual_profile_prefix(source);
    if(prefix){
        char tail[sizeof(source)];size_t tail_bytes=strlen(source+prefix)+1;
        memcpy(tail,source+prefix,tail_bytes);memcpy(source,tail,tail_bytes);profile_floor=2;
    }
    size_t length=strlen(source);int trailing=length && (source[length-1]=='/' || source[length-1]=='\\');
    if(trailing && mode!=2)return 3;
    if(source[0]=='/' || source[0]=='\\' || strchr(source,':'))return 50;
    size_t used=0,depth=0,starts[256];
    if(profile_floor){memcpy(path,"UserData/FNV",sizeof("UserData/FNV")-1);used=sizeof("UserData/FNV")-1;
        starts[0]=0;starts[1]=8;depth=profile_floor;}
    for(size_t start=0;source[start];){size_t end=start;while(source[end] && source[end]!='/' && source[end]!='\\')end++;
        size_t n=end-start;
        if(n==1 && source[start]=='.'){}
        else if(n==2 && source[start]=='.' && source[start+1]=='.'){
            if(depth<=profile_floor)return 5;
            used=starts[--depth];
        }else if(n){size_t original=used;if(used)path[used++]='/';
            for(size_t i=0;i<n;++i){unsigned c=(unsigned char)source[start+i];
                if((c=='?' && mode==1) || (c=='*' && mode==1 && source[end]))return 50;
                if(c<32 || strchr("?\"<>|",(int)c) || (c=='*' && mode!=1))return 123;
                path[used++]=(char)c;}
            starts[depth++]=original;
        }
        start=source[end]?end+1:end;
    }
    if(!used){if(mode!=2)return 5;path[used++]='.';}
    if(trailing && mode==2)path[used++]='/';
    path[used]=0;
    for(size_t i=0;i<=used;++i){unsigned c=(unsigned char)path[i];key[i]=(char)(c>='A'&&c<='Z'?c+32:c);}
    return 0;
}
static int file_path(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key){return file_path_mode(p,at,wide,path,key,0);}
/* Called exactly once after live becomes false and the last operation unpins.
 * The slot is unavailable until fclose has finished; no table lock during IO. */
static void file_finish_close(struct fnv_windows_process *p,struct file_slot *f){
    if(f->kind==1)fclose(f->stream);else fnv_host_directory_close(f->search);
    fnv_host_mutex_lock(&p->files_guard);f->stream=NULL;f->search=NULL;
    if(f->generation==p->file_generation_max)p->file_stats.retired_slots++;
    else {uint32_t i=(uint32_t)(f-p->files);f->next=UINT32_MAX;
        if(p->file_tail!=UINT32_MAX)p->files[p->file_tail].next=i;else p->file_free=i;
        p->file_tail=i;
    }
    fnv_host_mutex_unlock(&p->files_guard);
}
static int files_close(struct fnv_windows_process *p,uint32_t handle,uint32_t *value){
    if((handle&0xf0000000u)!=0xd0000000u)return 0;
    *value=0;if(!p->files)return 1;
    fnv_host_mutex_lock(&p->files_guard);struct file_slot *f=file_lookup(p,handle);int finish=0;
    if(f){f->live=0;p->file_stats.live_handles--;finish=!f->pins;*value=1;}
    fnv_host_mutex_unlock(&p->files_guard);if(finish)file_finish_close(p,f);return 1;
}
static struct file_slot *file_acquire_kind(struct fnv_windows_process *p,uint32_t handle,uint32_t kind){
    uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);
    fnv_host_mutex_lock(&p->files_guard);struct file_slot *f=file_lookup(p,handle);
    if(f && f->kind==kind && f->pins!=UINT32_MAX)f->pins++;else f=NULL;
    fnv_host_mutex_unlock(&p->files_guard);
    if(f){fnv_host_mutex_lock(&f->io_guard);
        if(timed && fnv_host_now_ns(&end) && end>=start){fnv_host_mutex_lock(&p->files_guard);p->file_stats.io_wait_ns+=end-start;fnv_host_mutex_unlock(&p->files_guard);}}
    return f;
}
static struct file_slot *file_acquire(struct fnv_windows_process *p,uint32_t handle){return file_acquire_kind(p,handle,1);}
static void file_release(struct fnv_windows_process *p,struct file_slot *f){
    fnv_host_mutex_unlock(&f->io_guard);fnv_host_mutex_lock(&p->files_guard);
    int finish=!--f->pins && !f->live;fnv_host_mutex_unlock(&p->files_guard);if(finish)file_finish_close(p,f);
}
enum fnv_cpu_result fnv_windows_file_stats(struct fnv_windows_process *p,struct fnv_windows_file_stats *out){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_file_stats s={0};if(p->files){fnv_host_mutex_lock(&p->files_guard);s=p->file_stats;fnv_host_mutex_unlock(&p->files_guard);}
    *out=s;return FNV_CPU_OK;
}
static void file_create(int wide){
    uint32_t at=fnv_cpu_argument_u32(0),access=fnv_cpu_argument_u32(1),share=fnv_cpu_argument_u32(2),security=fnv_cpu_argument_u32(3);
    uint32_t disposition=fnv_cpu_argument_u32(4),flags=fnv_cpu_argument_u32(5),template=fnv_cpu_argument_u32(6);
    struct fnv_windows_process *p=file_process();uint32_t error=0,handle=UINT32_MAX;char path[512],key[512],full[1538];
    if(access!=0x80000000u || security || template || disposition!=3 || (share&~7u) || (flags&~0x1800ffffu))error=50;
    else error=(uint32_t)file_path(p,at,wide,path,key);
    if(!error){size_t n=strlen(p->file_root);memcpy(full,p->file_root,n);full[n++]='/';memcpy(full+n,path,strlen(path)+1);
        errno=0;FILE *stream=fopen(full,"rb");
        if(!stream){error=fnv_host_file_error(errno);
            if(error==2){char *last=strrchr(full,'/');if(last){*last=0;if(!fnv_host_file_directory(full))error=3;}}}
        else {uint64_t size;if(!fnv_host_file_size(stream,&size))error=5;
            if(!error){fnv_host_mutex_lock(&p->files_guard);
                for(uint32_t i=0;i<p->config.file_handle_capacity;++i){struct file_slot *other=p->files+i;
                    if(other->stream && !strcmp(other->key,key) && (!(share&1) || !(other->share&1))){error=32;break;}}
                if(!error && p->file_free==UINT32_MAX)error=4;
                if(!error){uint32_t i=p->file_free;struct file_slot *f=p->files+i;p->file_free=f->next;if(p->file_free==UINT32_MAX)p->file_tail=UINT32_MAX;
                    f->generation++;f->live=1;f->kind=1;f->share=share;f->stream=stream;memcpy(f->key,key,strlen(key)+1);
                    handle=0xd0000000u|(f->generation<<(p->file_bits+2))|(i<<2);p->file_stats.live_handles++;p->file_stats.opens++;stream=NULL;
                }
                fnv_host_mutex_unlock(&p->files_guard);
            }
            if(stream)fclose(stream);
        }
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(handle,28);
}
static void api_file_create_a(void){file_create(0);}static void api_file_create_w(void){file_create(1);}
static void api_file_read(void){
    uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2),out=fnv_cpu_argument_u32(3),overlapped=fnv_cpu_argument_u32(4);
    struct fnv_windows_process *p=file_process();uint32_t value=0,error=0;
    unsigned char *count=out?file_pointer(p,out,4,1):NULL;if(count)write32(count,0);
    if(!count)error=87;else if(overlapped)error=50;
    else {unsigned char *buffer=file_pointer(p,at,bytes,1);struct file_slot *f=file_acquire(p,h);
        if(!f)error=6;
        else {uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);size_t got=0;
            errno=0;if(bytes)got=fread(buffer,1,bytes,f->stream);
            int read_error=errno,failed=ferror(f->stream);if(failed){error=fnv_host_file_error(read_error);clearerr(f->stream);}else value=1;
            write32(count,(uint32_t)got);int measured=timed && fnv_host_now_ns(&end) && end>=start;
            fnv_host_mutex_lock(&p->files_guard);p->file_stats.reads++;p->file_stats.read_bytes+=got;p->file_stats.read_errors+=failed!=0;
            if(measured)p->file_stats.read_ns+=end-start;
            fnv_host_mutex_unlock(&p->files_guard);file_release(p,f);
        }
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,20);
}
static void file_size(int extended){
    uint32_t h=fnv_cpu_argument_u32(0),out=fnv_cpu_argument_u32(1),value=extended?0:UINT32_MAX,error=0;
    struct fnv_windows_process *p=file_process();unsigned char *output=out?file_pointer(p,out,extended?8:4,1):NULL;
    if(extended && !output)sync_invalid("GetFileSizeEx result is NULL",out);
    struct file_slot *f=file_acquire(p,h);uint64_t size=0;
    if(!f)error=6;else {if(!fnv_host_file_size(f->stream,&size))error=fnv_host_file_error(errno);file_release(p,f);}
    if(!error){value=extended?1:(uint32_t)size;
        if(output){write32(output,extended?(uint32_t)size:(uint32_t)(size>>32));if(extended)write32(output+4,(uint32_t)(size>>32));}
        if(!extended && value==UINT32_MAX)fnv_windows_set_last_error(0);
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
static void api_file_size(void){file_size(0);}static void api_file_size_ex(void){file_size(1);}
static void file_seek(int extended){
    uint32_t h=fnv_cpu_argument_u32(0),low=fnv_cpu_argument_u32(1),third=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=file_process();unsigned char *output=NULL;uint32_t method;int64_t distance;
    if(extended){uint32_t out=fnv_cpu_argument_u32(3);method=fnv_cpu_argument_u32(4);output=out?file_pointer(p,out,8,1):NULL;
        distance=(int64_t)(((uint64_t)third<<32)|low);
    }else {method=fnv_cpu_argument_u32(3);output=third?file_pointer(p,third,4,1):NULL;
        distance=output?(int64_t)(((uint64_t)read32(output)<<32)|low):(int64_t)(int32_t)low;
    }
    uint32_t value=extended?0:UINT32_MAX,error=method>2?87:0;uint64_t base=0,target=0;
    if(!error){struct file_slot *f=file_acquire(p,h);
        if(!f)error=6;
        else {if(method==1){int64_t pos=fnv_host_file_tell(f->stream);if(pos<0)error=fnv_host_file_error(errno);else base=(uint64_t)pos;}
            else if(method==2 && !fnv_host_file_size(f->stream,&base))error=fnv_host_file_error(errno);
            if(!error){if(distance<0){uint64_t magnitude=(uint64_t)(-(distance+1))+1;if(magnitude>base)error=131;else target=base-magnitude;}
                else if(base>(uint64_t)INT64_MAX-(uint64_t)distance)error=87;else target=base+(uint64_t)distance;}
            if(!error && !fnv_host_file_seek(f->stream,(int64_t)target))error=fnv_host_file_error(errno);
            file_release(p,f);
        }
    }
    if(!error){value=extended?1:(uint32_t)target;if(output){write32(output,extended?(uint32_t)target:(uint32_t)(target>>32));if(extended)write32(output+4,(uint32_t)(target>>32));}
        if(!extended && value==UINT32_MAX)fnv_windows_set_last_error(0);
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,extended?20:16);
}
static void api_file_seek(void){file_seek(0);}static void api_file_seek_ex(void){file_seek(1);}

