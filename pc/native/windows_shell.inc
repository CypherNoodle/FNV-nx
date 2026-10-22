/* Captured SHGetFolderPathA requests: current LocalAppData/Documents, null HWND/token.
 * The guest path is stable and maps only below the user-data sandbox. */
static void shell_guest_profile_folder(void){
    uint32_t hwnd=fnv_cpu_argument_u32(0),csidl=fnv_cpu_argument_u32(1);
    uint32_t token=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3),output=fnv_cpu_argument_u32(4);
    struct fnv_windows_process *p=file_process();
    if(hwnd || (csidl!=0x1cu && csidl!=5u) || token || flags)
        sync_invalid("Windows SHGetFolderPathA variant pending",csidl);
    unsigned char *destination=file_pointer(p,output,260,1);
    const char *path=csidl==5u?"C:\\Users\\FNV\\Documents":"C:\\Users\\FNV\\AppData\\Local";
    static const char *const appdata[]={"UserData","UserData/FNV","UserData/FNV/AppData","UserData/FNV/AppData/Local"};
    static const char *const documents[]={"UserData","UserData/FNV","UserData/FNV/Documents"};
    const char *const *directories=csidl==5u?documents:appdata;
    size_t count=csidl==5u?sizeof(documents)/sizeof(documents[0]):sizeof(appdata)/sizeof(appdata[0]);
    char full[1538];size_t root=strlen(p->file_root);
    if(root+1+strlen(directories[count-1])>=sizeof(full))sync_invalid("Windows profile folder path length pending",output);
    for(size_t i=0;i<count;++i){
        int n=snprintf(full,sizeof(full),"%s/%s",p->file_root,directories[i]);
        if(n<0 || (size_t)n>=sizeof(full))sync_invalid("Windows profile folder path length pending",output);
        errno=0;if(!fnv_host_file_mkdir(full)){
            uint32_t error=fnv_host_file_error(errno);fnv_cpu_return_u32(0x80070000u|error,20);return;
        }
    }
    memset(destination,0,260);memcpy(destination,path,strlen(path)+1);
    fnv_cpu_return_u32(0,20); /* S_OK; preserve guest LastError. */
}
static void shell_read_ascii(uint32_t address,char *out,size_t capacity){
    struct fnv_windows_process *p=file_process();
    if(!address || !capacity)sync_invalid("Windows ShellExecute path unavailable",address);
    for(size_t i=0;i<capacity;i++){
        uint64_t pos=(uint64_t)address+i;
        unsigned char *b=pos<=UINT32_MAX?file_pointer(p,(uint32_t)pos,1,0):NULL;
        if(!b)sync_invalid("Windows ShellExecute path range unavailable",address);
        unsigned c=*b;
        if(c>127)sync_invalid("Windows ShellExecute path encoding pending",address);
        out[i]=(char)c;
        if(!c)return;
    }
    sync_invalid("Windows ShellExecute path length pending",address);
}
static int shell_ascii_equal(const char *a,const char *b){
    while(*a && *b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A' && x<='Z')x+=32;
        if(y>='A' && y<='Z')y+=32;
        if(x!=y)return 0;
    }return !*a && !*b;
}
/* This port starts FalloutNV.exe directly and has no Windows process launcher.
 * The captured guest checks ShellExecuteA's result against zero and continues
 * engine startup on failure. Return a failure only for its launcher request;
 * never claim that a child process was started. */
static void shell_execute_a(void){
    uint32_t window=fnv_cpu_argument_u32(0),operation=fnv_cpu_argument_u32(1);
    uint32_t file=fnv_cpu_argument_u32(2),parameters=fnv_cpu_argument_u32(3);
    uint32_t directory=fnv_cpu_argument_u32(4),show=fnv_cpu_argument_u32(5);
    if(window || operation || parameters || directory || show!=1)
        sync_invalid("Windows ShellExecuteA variant pending",show);
    char path[1024];shell_read_ascii(file,path,sizeof(path));
    static const char launcher[]="FalloutNVLauncher.exe";
    size_t path_bytes=strlen(path),launcher_bytes=sizeof(launcher)-1;
    if(path_bytes<launcher_bytes || !shell_ascii_equal(path+path_bytes-launcher_bytes,launcher))
        sync_invalid("Windows ShellExecuteA target pending",file);
    fnv_windows_set_last_error(2); /* ERROR_FILE_NOT_FOUND: launcher is absent. */
    fnv_cpu_return_u32(0,24); /* Failure takes the guest's direct-engine path. */
}
static const struct fnv_cpu_export shell_exports[]={
    {"shell32.dll","SHGetFolderPathA",0,0,shell_guest_profile_folder},
    {"shell32.dll","ShellExecuteA",0,0,shell_execute_a}
};
const struct fnv_cpu_export *fnv_windows_shell_exports(size_t *count){
    if(count)*count=sizeof(shell_exports)/sizeof(*shell_exports);
    return shell_exports;
}
