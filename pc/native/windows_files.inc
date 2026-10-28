/* Synchronous guest file ABI. Game assets stay read-only; profile writes are
 * confined to the virtual UserData tree. Separate table and per-file
 * IO locks: no SD operation holds kernel_guard, process guard, or files_guard. */
#include "host_file.h"
#include "host_directory.h"
#include <string.h>
struct file_slot {
    FILE *stream;struct fnv_host_directory *search;fnv_host_mutex io_guard;fnv_host_condition idle;
    uint32_t generation,live,pins,next,share,access,kind,search_done,opening,closing,last_io,inheritable;
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
            while(i){--i;fnv_host_condition_destroy(&items[i].idle);fnv_host_mutex_destroy(&items[i].io_guard);}
            fnv_host_mutex_destroy(&p->files_guard);free(root);free(items);return FNV_CPU_NO_MEMORY;
        }
        if(!fnv_host_condition_init(&items[i].idle)){
            fnv_host_mutex_destroy(&items[i].io_guard);
            while(i){--i;fnv_host_condition_destroy(&items[i].idle);fnv_host_mutex_destroy(&items[i].io_guard);}
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
        fnv_host_condition_destroy(&p->files[i].idle);fnv_host_mutex_destroy(&p->files[i].io_guard);}
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
/* Guest identity of file_root. This is not an absolute path on the host. */
static const char file_virtual_game_root[]="C:\\FNV-nx";
static size_t file_virtual_game_prefix(const char *source){
    size_t n=sizeof(file_virtual_game_root)-1;
    for(size_t i=0;i<n;++i){unsigned a=(unsigned char)source[i],b=(unsigned char)file_virtual_game_root[i];
        if(!a)return 0;
        if(a=='/')a='\\';
        if(a>='A' && a<='Z')a+=32;
        if(b>='A' && b<='Z')b+=32;
        if(a!=b)return 0;
    }
    return !source[n] || source[n]=='/' || source[n]=='\\'?n:0;
}
static int file_path_mode(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key,int mode,int *profile){
    char source[512];int ended=0;
    for(size_t i=0;i<sizeof(source);++i){uint64_t pos=(uint64_t)at+i*(wide?2:1);unsigned width=wide?2:1;
        if(pos+width>UINT64_C(0x100000000))sync_invalid("Windows file path wraps",at);
        unsigned char *b=file_pointer(p,(uint32_t)pos,width,0);unsigned c=b[0]|(wide?((unsigned)b[1]<<8):0);
        if(c>127)return 123;
        source[i]=(char)c;if(!c){ended=1;break;}
    }
    if(!ended || !source[0])return 123;
    size_t profile_floor=0,prefix=file_virtual_profile_prefix(source);
    if(profile)*profile=prefix!=0;
    if(prefix){
        char tail[sizeof(source)];size_t tail_bytes=strlen(source+prefix)+1;
        memcpy(tail,source+prefix,tail_bytes);memcpy(source,tail,tail_bytes);profile_floor=2;
    }else if((prefix=file_virtual_game_prefix(source))!=0){
        if(source[prefix])prefix++;
        memmove(source,source+prefix,strlen(source+prefix)+1);
        if(!source[0]){source[0]='.';source[1]=0;}
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
static int file_path_mode_basic(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key,int mode){return file_path_mode(p,at,wide,path,key,mode,NULL);}
static int file_path(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key){return file_path_mode(p,at,wide,path,key,0,NULL);}
static int file_path_with_profile(struct fnv_windows_process *p,uint32_t at,int wide,char *path,char *key,int *profile){return file_path_mode(p,at,wide,path,key,0,profile);}
static int file_sync_account(struct fnv_windows_process *p,FILE *stream){
    uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);errno=0;int success=fnv_host_file_sync(stream);int saved=errno;
    int measured=timed && fnv_host_now_ns(&end) && end>=start;
    fnv_host_mutex_lock(&p->files_guard);p->file_stats.syncs++;p->file_stats.sync_errors+=success==0;
    if(measured)p->file_stats.sync_ns+=end-start;
    fnv_host_mutex_unlock(&p->files_guard);errno=saved;return success;
}
/* Called exactly once after live becomes false and the last operation unpins.
 * The slot is unavailable until fclose has finished; no table lock during IO. */
static uint32_t file_finish_close(struct fnv_windows_process *p,struct file_slot *f){
    uint32_t error=0;
    if(f->kind==1){uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);errno=0;int failed=fnv_host_file_close(f->stream)!=0;int saved=errno;
        if(failed)error=fnv_host_file_error(saved);
        int measured=timed && fnv_host_now_ns(&end) && end>=start;
        fnv_host_mutex_lock(&p->files_guard);p->file_stats.closes++;p->file_stats.close_errors+=failed!=0;
        if(measured)p->file_stats.close_ns+=end-start;
        fnv_host_mutex_unlock(&p->files_guard);
    }
    else fnv_host_directory_close(f->search);
    fnv_host_mutex_lock(&p->files_guard);f->stream=NULL;f->search=NULL;f->closing=0;f->last_io=0;
    if(f->generation==p->file_generation_max)p->file_stats.retired_slots++;
    else {uint32_t i=(uint32_t)(f-p->files);f->next=UINT32_MAX;
        if(p->file_tail!=UINT32_MAX)p->files[p->file_tail].next=i;else p->file_free=i;
        p->file_tail=i;
    }
    fnv_host_mutex_unlock(&p->files_guard);return error;
}
#ifdef FNV_TEST_FILE_IO
extern void fnv_test_file_close_wait(void);
extern void fnv_test_file_io_gate(uint32_t operation);
#endif
static int files_close(struct fnv_windows_process *p,uint32_t handle,uint32_t *value,uint32_t *error){
    if((handle&0xf0000000u)!=0xd0000000u)return 0;
    *value=0;*error=6;if(!p->files)return 1;
    fnv_host_mutex_lock(&p->files_guard);struct file_slot *f=file_lookup(p,handle);
    if(!f){fnv_host_mutex_unlock(&p->files_guard);return 1;}
    f->live=0;f->closing=1;p->file_stats.live_handles--;
    /* Stop new operations, then wait for every already-pinned operation. The
     * closer alone owns final flush/close and reports their actual errors. */
    while(f->pins){
#ifdef FNV_TEST_FILE_IO
        fnv_test_file_close_wait();
#endif
        if(fnv_host_condition_wait(&f->idle,&p->files_guard)<0){
            f->live=1;f->closing=0;p->file_stats.live_handles++;*error=1117;
            fnv_host_mutex_unlock(&p->files_guard);return 1;
        }
    }
    fnv_host_mutex_unlock(&p->files_guard);*error=0;
    /* fclose flushes stdio data and reports backend errors. Durability sync is
     * reserved for the guest's explicit FlushFileBuffers call. */
    uint32_t closed=file_finish_close(p,f);if(!*error)*error=closed;
    *value=!*error;return 1;
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
    int finish=!--f->pins && !f->live && !f->closing;
    if(!f->pins && f->closing)fnv_host_condition_wake_all(&f->idle);
    fnv_host_mutex_unlock(&p->files_guard);if(finish)(void)file_finish_close(p,f);
}
static int file_share_compatible(uint32_t access,uint32_t share,const struct file_slot *other){
    return !(((access&0x80000000u) && !(other->share&1)) || ((access&0x40000000u) && !(other->share&2)) ||
             ((other->access&0x80000000u) && !(share&1)) || ((other->access&0x40000000u) && !(share&2)));
}
static struct file_slot *file_reserve_open(struct fnv_windows_process *p,uint32_t access,uint32_t share,const char *key,uint32_t *error){
    struct file_slot *reserved=NULL;fnv_host_mutex_lock(&p->files_guard);
    for(uint32_t i=0;i<p->config.file_handle_capacity;++i){struct file_slot *other=p->files+i;
        if((other->stream || other->opening) && !strcmp(other->key,key) && !file_share_compatible(access,share,other)){*error=32;break;}}
    if(!*error && p->file_free==UINT32_MAX)*error=4;
    if(!*error){uint32_t i=p->file_free;reserved=p->files+i;p->file_free=reserved->next;if(p->file_free==UINT32_MAX)p->file_tail=UINT32_MAX;
        reserved->next=UINT32_MAX;reserved->opening=1;reserved->share=share;reserved->access=access;memcpy(reserved->key,key,strlen(key)+1);}
    fnv_host_mutex_unlock(&p->files_guard);return reserved;
}
static void file_cancel_open(struct fnv_windows_process *p,struct file_slot *f){
    if(!f)return;
    fnv_host_mutex_lock(&p->files_guard);f->opening=0;f->share=0;f->access=0;f->key[0]=0;
    uint32_t i=(uint32_t)(f-p->files);if(p->file_tail!=UINT32_MAX)p->files[p->file_tail].next=i;else p->file_free=i;
    p->file_tail=i;fnv_host_mutex_unlock(&p->files_guard);
}
static enum fnv_cpu_result fnv_windows_file_stats_snapshot(struct fnv_windows_process *p,struct fnv_windows_file_stats *out,int nonblocking){
    if(!p || !out)return FNV_CPU_INVALID;
    struct fnv_windows_file_stats s={0};if(p->files){if(nonblocking){if(!fnv_host_mutex_trylock(&p->files_guard))return FNV_CPU_BUSY;}else fnv_host_mutex_lock(&p->files_guard);s=p->file_stats;fnv_host_mutex_unlock(&p->files_guard);}
    *out=s;return FNV_CPU_OK;
}
enum fnv_cpu_result fnv_windows_file_stats(struct fnv_windows_process *p,struct fnv_windows_file_stats *out){return fnv_windows_file_stats_snapshot(p,out,0);}
static void file_create(int wide){
    uint32_t at=fnv_cpu_argument_u32(0),access=fnv_cpu_argument_u32(1),share=fnv_cpu_argument_u32(2),security=fnv_cpu_argument_u32(3);
    uint32_t disposition=fnv_cpu_argument_u32(4),flags=fnv_cpu_argument_u32(5),template=fnv_cpu_argument_u32(6);
    struct fnv_windows_process *p=file_process();uint32_t error=0,success_error=0,handle=UINT32_MAX,inheritable=0;char path[512],key[512],full[1538];int profile=0;
    int writable=access==0x40000000u || access==0xc0000000u;
    if((access!=0x80000000u && !writable) || template || disposition<1 || disposition>5 || (share&~7u) || (flags&~0x1800ffffu))error=50;
    else if(security){
        /* Packed SECURITY_ATTRIBUTES32, never a native host pointer. A NULL
         * descriptor uses the host filesystem's normal access checks. Guest
         * process creation is unsupported; retain the inheritance bit as guest
         * metadata rather than forwarding it to the host FILE descriptor. */
        const unsigned char *attributes=file_pointer(p,security,12,0);
        if(read32(attributes)!=12 || read32(attributes+4))error=50;
        else inheritable=read32(attributes+8)!=0;
    }
    if(!error)error=(uint32_t)file_path_with_profile(p,at,wide,path,key,&profile);
    if(!error && (writable ? !profile : disposition!=3))error=50;
    if(!error){struct file_slot *reserved=file_reserve_open(p,access,share,key,&error);
        if(reserved){size_t n=strlen(p->file_root);memcpy(full,p->file_root,n);full[n++]='/';memcpy(full+n,path,strlen(path)+1);
            int existed=fnv_host_file_exists(full);
            errno=0;FILE *stream=fnv_host_file_open(full,writable?disposition:0);
            if(!stream){error=fnv_host_file_error(errno);
                if(error==2){char *last=strrchr(full,'/');if(last){*last=0;if(!fnv_host_file_directory(full))error=3;}}}
            else {uint64_t size;if(!fnv_host_file_size(stream,&size))error=5;
                if(!error){fnv_host_mutex_lock(&p->files_guard);reserved->generation++;reserved->live=1;reserved->opening=0;
                    reserved->kind=1;reserved->stream=stream;reserved->inheritable=inheritable;handle=0xd0000000u|(reserved->generation<<(p->file_bits+2))|((uint32_t)(reserved-p->files)<<2);
                    p->file_stats.live_handles++;p->file_stats.opens++;stream=NULL;fnv_host_mutex_unlock(&p->files_guard);
                    if(existed && (disposition==2 || disposition==4))success_error=183;}
                if(stream)fclose(stream);
            }
            if(error)file_cancel_open(p,reserved);
        }
    }
    fnv_windows_set_last_error(error?error:success_error);
    fnv_cpu_return_u32(handle,28);
}
static void api_file_create_a(void){file_create(0);}static void api_file_create_w(void){file_create(1);}
/* The observed boot path requests an exclusive configuration copy. Overwrite
 * and special source attributes remain explicitly unsupported, never truncated.
 * Reservations cover the whole copy without holding the file-table lock on IO. */
static void api_file_copy_a(void){
    uint32_t source=fnv_cpu_argument_u32(0),target=fnv_cpu_argument_u32(1),exclusive=fnv_cpu_argument_u32(2);
    struct fnv_windows_process *p=file_process();uint32_t error=0,value=0;int profile=0;
    char src[512],src_key[512],dst[512],dst_key[512],src_full[1538],dst_full[1538];
    struct file_slot *src_slot=NULL,*dst_slot=NULL;FILE *input=NULL,*output=NULL;unsigned char *buffer=NULL;int created=0;
    error=(uint32_t)file_path_with_profile(p,source,0,src,src_key,NULL);
    if(!error)error=(uint32_t)file_path_with_profile(p,target,0,dst,dst_key,&profile);
    if(!error && !profile)error=50;
    if(!error && !strcmp(src_key,dst_key))error=5;
    if(!error && !exclusive)error=50;
    if(!error){size_t n=strlen(p->file_root);
        memcpy(src_full,p->file_root,n);src_full[n]='/';memcpy(src_full+n+1,src,strlen(src)+1);
        memcpy(dst_full,p->file_root,n);dst_full[n]='/';memcpy(dst_full+n+1,dst,strlen(dst)+1);
        src_slot=file_reserve_open(p,0x80000000u,1,src_key,&error);
    }
    if(!error){struct fnv_host_entry entry;error=fnv_host_metadata(src_full,&entry);
        if(!error && (entry.attributes&16u))error=5;
        if(!error && (entry.attributes&~(32u|128u)))error=50;
    }
    if(!error){errno=0;input=fnv_host_file_open(src_full,0);if(!input)error=fnv_host_file_error(errno);}
    uint64_t remaining=0;
    if(!error && !fnv_host_file_size(input,&remaining))error=50;
    if(!error){buffer=malloc(65536);if(!buffer)error=8;}
    if(!error)dst_slot=file_reserve_open(p,0x40000000u,0,dst_key,&error);
    if(!error){errno=0;output=fnv_host_file_open(dst_full,1);
        if(!output)error=fnv_host_file_error(errno);else created=1;}
    while(!error && remaining){size_t count=remaining>65536?65536:(size_t)remaining;
        errno=0;size_t got=fread(buffer,1,count,input);if(got!=count){error=fnv_host_file_error(errno);break;}
        errno=0;if(fwrite(buffer,1,count,output)!=count){error=fnv_host_file_error(errno);break;}remaining-=count;
    }
    if(output){errno=0;if(!error && !file_sync_account(p,output))error=fnv_host_file_error(errno);
        errno=0;if(fclose(output) && !error)error=fnv_host_file_error(errno);}
    if(input){errno=0;if(fclose(input) && !error)error=fnv_host_file_error(errno);}
    /* Only an exclusively created destination can be removed on failure. */
    if(error && created && !fnv_host_file_remove(dst_full))error=1117;
    free(buffer);if(dst_slot)file_cancel_open(p,dst_slot);if(src_slot)file_cancel_open(p,src_slot);
    if(error)fnv_windows_set_last_error(error);else value=1;
    fnv_cpu_return_u32(value,12);
}
static void api_file_delete_a(void){
    uint32_t at=fnv_cpu_argument_u32(0),value=0,error=0;int profile=0;
    struct fnv_windows_process *p=file_process();char path[512],key[512],full[1538];
    error=(uint32_t)file_path_with_profile(p,at,0,path,key,&profile);
    if(!error){size_t n=strlen(p->file_root);memcpy(full,p->file_root,n);full[n++]='/';memcpy(full+n,path,strlen(path)+1);
        struct fnv_host_entry entry;error=fnv_host_metadata(full,&entry);
        if(error==2){char parent[sizeof(full)];memcpy(parent,full,strlen(full)+1);char *last=strrchr(parent,'/');
            if(last){*last=0;if(!fnv_host_file_directory(parent))error=3;}}
        if(!error && (!profile || (entry.attributes&17u)))error=5;
        if(!error){
            /* Deferred deletion of a file still open with SHARE_DELETE needs
             * a pending-delete state; report that unsupported case explicitly.
             * The zero-share reservation blocks new opens during actual IO. */
            uint32_t shared_delete=0;fnv_host_mutex_lock(&p->files_guard);
            for(uint32_t i=0;i<p->config.file_handle_capacity;++i){struct file_slot *f=p->files+i;
                if((f->stream || f->opening) && !strcmp(f->key,key)){
                    if(!(f->share&4u)){error=32;break;}shared_delete=1;
                }}
            fnv_host_mutex_unlock(&p->files_guard);
            if(!error && shared_delete)error=50;
            struct file_slot *reserved=error?NULL:file_reserve_open(p,0,0,key,&error);
            if(reserved){errno=0;if(fnv_host_file_remove(full))value=1;else error=fnv_host_file_error(errno);
                file_cancel_open(p,reserved);}
        }
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,4);
}
static int file_prepare_io(struct file_slot *f,uint32_t operation){
    if(f->last_io && f->last_io!=operation){
        errno=0;int64_t at=fnv_host_file_tell(f->stream);
        if(at<0 || !fnv_host_file_seek(f->stream,at))return 0;
    }
    f->last_io=operation;
#ifdef FNV_TEST_FILE_IO
    fnv_test_file_io_gate(operation);
#endif
    return 1;
}
static void api_file_write(void){
    uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2),out=fnv_cpu_argument_u32(3),overlapped=fnv_cpu_argument_u32(4);
    struct fnv_windows_process *p=file_process();uint32_t value=0,error=0;unsigned char *count=out?file_pointer(p,out,4,1):NULL;
    if(count)write32(count,0);
    if(!count)error=87;else if(overlapped)error=50;
    else {unsigned char *buffer=bytes?file_pointer(p,at,bytes,0):NULL;struct file_slot *f=file_acquire(p,h);
        if(!f)error=6;
        else if(!(f->access&0x40000000u)){error=5;file_release(p,f);}
        else {errno=0;size_t written=0;uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);
            if(bytes && !file_prepare_io(f,2))error=fnv_host_file_error(errno);
            else if(bytes)written=fwrite(buffer,1,bytes,f->stream);
            if(!error && (written!=bytes || ferror(f->stream))){error=fnv_host_file_error(errno);clearerr(f->stream);}
            else if(!error){write32(count,(uint32_t)written);value=1;}
            int measured=timed && fnv_host_now_ns(&end) && end>=start;
            fnv_host_mutex_lock(&p->files_guard);p->file_stats.writes++;p->file_stats.write_bytes+=written;p->file_stats.write_errors+=error!=0;
            if(measured)p->file_stats.write_ns+=end-start;
            fnv_host_mutex_unlock(&p->files_guard);
            file_release(p,f);}
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,20);
}
static void api_file_flush(void){
    uint32_t h=fnv_cpu_argument_u32(0),value=0,error=0;struct fnv_windows_process *p=file_process();
    struct file_slot *f=file_acquire(p,h);
    if(!f)error=6;
    else if(!(f->access&0x40000000u)){error=5;file_release(p,f);}
    else {if(file_prepare_io(f,2) && file_sync_account(p,f->stream))value=1;else error=fnv_host_file_error(errno);file_release(p,f);}
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,4);
}
static void api_file_set_eof(void){
    uint32_t h=fnv_cpu_argument_u32(0),value=0,error=0;struct fnv_windows_process *p=file_process();
    struct file_slot *f=file_acquire(p,h);
    if(!f)error=6;
    else if(!(f->access&0x40000000u)){error=5;file_release(p,f);}
    else {errno=0;int ready=file_prepare_io(f,2);int64_t position=ready?fnv_host_file_tell(f->stream):-1;
        if(position<0 || !fnv_host_file_truncate(f->stream,(uint64_t)position))error=fnv_host_file_error(errno);
        else value=1;
        file_release(p,f);
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,4);
}
static void api_file_create_directory_a(void){
    uint32_t at=fnv_cpu_argument_u32(0),security=fnv_cpu_argument_u32(1),value=0,error=0;int profile=0;
    struct fnv_windows_process *p=file_process();char path[512],key[512],full[1538];
    if(security)error=50;
    else error=(uint32_t)file_path_mode(p,at,0,path,key,2,&profile);
    if(!error && !profile)error=50;
    if(!error){size_t n=strlen(p->file_root);memcpy(full,p->file_root,n);full[n++]='/';memcpy(full+n,path,strlen(path)+1);
        if(fnv_host_file_create_directory(full))value=1;
        else {error=errno==EEXIST?183:fnv_host_file_error(errno);if(error==2){char *last=strrchr(full,'/');if(last){*last=0;if(!fnv_host_file_directory(full))error=3;}}}
    }
    if(error)fnv_windows_set_last_error(error);
    fnv_cpu_return_u32(value,8);
}
static void api_file_read(void){
    uint32_t h=fnv_cpu_argument_u32(0),at=fnv_cpu_argument_u32(1),bytes=fnv_cpu_argument_u32(2),out=fnv_cpu_argument_u32(3),overlapped=fnv_cpu_argument_u32(4);
    struct fnv_windows_process *p=file_process();uint32_t value=0,error=0;
    unsigned char *count=out?file_pointer(p,out,4,1):NULL;if(count)write32(count,0);
    if(!count)error=87;else if(overlapped)error=50;
    else {unsigned char *buffer=bytes?file_pointer(p,at,bytes,1):NULL;struct file_slot *f=file_acquire(p,h);
        if(!f)error=6;
        else if(!(f->access&0x80000000u)){error=5;file_release(p,f);}
        else {uint64_t start=0,end=0;int timed=fnv_host_now_ns(&start);size_t got=0;
            errno=0;int ready=!bytes || file_prepare_io(f,1);if(ready && bytes)got=fread(buffer,1,bytes,f->stream);
            int read_error=errno,failed=!ready || ferror(f->stream);if(failed){error=fnv_host_file_error(read_error);clearerr(f->stream);}else value=1;
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
            if(!error){errno=0;if(!fnv_host_file_seek(f->stream,(int64_t)target))error=fnv_host_file_error(errno);else f->last_io=0;}
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
