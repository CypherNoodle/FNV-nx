/* Captured SHGetFolderPathA request: current LocalAppData, null HWND/token.
 * The guest path is stable and maps only below the user-data sandbox. */
static void shell_guest_local_appdata(void){
    uint32_t hwnd=fnv_cpu_argument_u32(0),csidl=fnv_cpu_argument_u32(1);
    uint32_t token=fnv_cpu_argument_u32(2),flags=fnv_cpu_argument_u32(3),output=fnv_cpu_argument_u32(4);
    struct fnv_windows_process *p=file_process();
    if(hwnd || csidl!=0x1cu || token || flags)
        sync_invalid("Windows SHGetFolderPathA variant pending",csidl);
    unsigned char *destination=file_pointer(p,output,260,1);
    static const char path[]="C:\\Users\\FNV\\AppData\\Local";
    static const char *const directories[]={"UserData","UserData/FNV","UserData/FNV/AppData","UserData/FNV/AppData/Local"};
    char full[1538];size_t root=strlen(p->file_root);
    if(root+1+sizeof(directories[3])-1>=sizeof(full))sync_invalid("Windows LocalAppData path length pending",output);
    for(size_t i=0;i<sizeof(directories)/sizeof(directories[0]);++i){
        int n=snprintf(full,sizeof(full),"%s/%s",p->file_root,directories[i]);
        if(n<0 || (size_t)n>=sizeof(full))sync_invalid("Windows LocalAppData path length pending",output);
        errno=0;if(!fnv_host_file_mkdir(full)){
            uint32_t error=fnv_host_file_error(errno);fnv_cpu_return_u32(0x80070000u|error,20);return;
        }
    }
    memset(destination,0,260);memcpy(destination,path,sizeof(path));
    fnv_cpu_return_u32(0,20); /* S_OK; preserve guest LastError. */
}
static const struct fnv_cpu_export shell_exports[]={
    {"shell32.dll","SHGetFolderPathA",0,0,shell_guest_local_appdata}
};
const struct fnv_cpu_export *fnv_windows_shell_exports(size_t *count){
    if(count)*count=sizeof(shell_exports)/sizeof(*shell_exports);
    return shell_exports;
}

