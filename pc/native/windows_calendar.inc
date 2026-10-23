/* Byte-wise guest access permits unaligned structures and overlapping buffers.
 * Snapshot the input before any output writes; invalid timestamps leave output
 * intact. Invalid guest memory follows the runtime's explicit fault policy. */
#include "host_calendar.h"
static void api_filetime_local(void){
    struct fnv_windows_process *p=thread()->process;
    uint32_t input=fnv_cpu_argument_u32(0),output=fnv_cpu_argument_u32(1);
    const unsigned char *src=file_pointer(p,input,8,0);
    uint64_t value=(uint64_t)read32(src)|((uint64_t)read32(src+4)<<32);
    unsigned char *dst=file_pointer(p,output,8,1);int32_t offset;
    if(!fnv_host_current_utc_offset(&offset))sync_invalid("Windows current timezone unavailable",input);
    value=fnv_calendar_local_filetime(value,offset);
    write32(dst,(uint32_t)value);write32(dst+4,(uint32_t)(value>>32));fnv_cpu_return_u32(1,8);
}
static void api_filetime_system(void){
    struct fnv_windows_process *p=thread()->process;
    uint32_t input=fnv_cpu_argument_u32(0),output=fnv_cpu_argument_u32(1);
    const unsigned char *src=file_pointer(p,input,8,0);
    uint64_t value=(uint64_t)read32(src)|((uint64_t)read32(src+4)<<32);uint16_t fields[8];
    unsigned char *dst=file_pointer(p,output,16,1);
    if(!fnv_calendar_systemtime(value,fields)){write32(thread()->teb+TEB_ERROR,87);fnv_cpu_return_u32(0,8);return;}
    for(unsigned i=0;i<8;++i){dst[i*2]=(unsigned char)fields[i];dst[i*2+1]=(unsigned char)(fields[i]>>8);}
    fnv_cpu_return_u32(1,8);
}
