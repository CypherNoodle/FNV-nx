#include <windows.h>
#include <mmsystem.h>
#include "windows_mmio.h"
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
#define BASE 0x400000u
#define BYTES 0x100000u
#define INFO (BASE+0x9000u)
#define NAME (BASE+0x9100u)
#define OUT_AT (BASE+0x10000u)
#define CK (BASE+0x9300u)
static unsigned char *arena,data[40000];static uint32_t data_bytes=sizeof(data),guest_closes,native_closes,guest_reads,native_reads;
static uint32_t get(uint32_t at){unsigned char *p=fnv_cpu_memory(at,4);return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint32_t at,uint32_t value){unsigned char *p=arena+at-BASE;for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static int32_t io_seek(int32_t current,int32_t at,uint32_t origin){int64_t n=(origin==0?0:origin==1?current:(int64_t)data_bytes)+at;return origin>2 || n<0 || n>INT32_MAX?-1:(int32_t)n;}
static LRESULT CALLBACK native_io(LPSTR value,UINT msg,LPARAM a,LPARAM b){
 MMIOINFO *p=(MMIOINFO *)value;
 if(msg==MMIOM_OPEN){if(strcmp((const char *)a,"synthetic"))return MMIOERR_FILENOTFOUND;p->lDiskOffset=0;p->adwInfo[0]=0x12345678;return 0;}
 if(msg==MMIOM_CLOSE){native_closes++;return 0;}
 if(msg==MMIOM_SEEK){int32_t n=io_seek(p->lDiskOffset,(int32_t)a,(uint32_t)b);if(n>=0)p->lDiskOffset=n;return n;}
 if(msg==MMIOM_READ){native_reads++;uint32_t n=p->lDiskOffset>=(int32_t)data_bytes?0:data_bytes-(uint32_t)p->lDiskOffset;if(n>(uint32_t)b)n=(uint32_t)b;memcpy((void *)a,data+p->lDiskOffset,n);p->lDiskOffset+=(LONG)n;return n;}
 assert(0);return -1;
}
static void test_io(void){uint32_t at=fnv_cpu_argument_u32(0),msg=fnv_cpu_argument_u32(1),a=fnv_cpu_argument_u32(2),b=fnv_cpu_argument_u32(3),answer=0;
 if(msg==3){if(strcmp(fnv_cpu_memory(a,1),"synthetic"))answer=257;else {put(at+44,0);put(at+48,0x12345678);}}
 else if(msg==4)guest_closes++;
 else if(msg==2){int32_t n=io_seek((int32_t)get(at+44),(int32_t)a,b);answer=(uint32_t)n;if(n>=0)put(at+44,(uint32_t)n);}
 else if(msg==0){guest_reads++;uint32_t pos=get(at+44),n=pos>=data_bytes?0:data_bytes-pos;if(n>b)n=b;if(n)memcpy(fnv_cpu_memory(a,n),data+pos,n);put(at+44,pos+n);answer=n;}
 else assert(0);
 fnv_cpu_return_u32(answer,16);
}
static const uint32_t counts[]={3,2,3,3,3,3,3,4,3,4};
static enum fnv_cpu_result invoke(unsigned api,const uint32_t args[4],uint32_t *out,struct fnv_cpu_fault *fault){
 struct fnv_cpu_state s;assert(fnv_cpu_get_state(&s)==FNV_CPU_OK);s.gpr[4]=BASE+0x3ff00;assert(fnv_cpu_set_state(&s)==FNV_CPU_OK);
 put(s.gpr[4],0x401500);for(unsigned i=0;i<4;i++)put(s.gpr[4]+4+i*4,args[i]);enum fnv_cpu_result r=fnv_cpu_execute(0x401500+api*32,fault);
 if(r==FNV_CPU_OK){assert(fnv_cpu_get_state(&s)==FNV_CPU_OK && s.gpr[4]==BASE+0x3ff04+counts[api]*4);*out=s.gpr[0];}return r;
}
static uint32_t call(unsigned api,uint32_t a,uint32_t b,uint32_t c,uint32_t d){uint32_t args[]={a,b,c,d},out=0;struct fnv_cpu_fault f;enum fnv_cpu_result r=invoke(api,args,&out,&f);if(r!=FNV_CPU_OK)fprintf(stderr,"MMIO %u failed: %s\n",api,f.text);assert(r==FNV_CPU_OK);return out;}
static void initial_info(void){memset(arena+INFO-BASE,0,72);put(INFO+4,mmioFOURCC('W','A','V',0));put(INFO+8,0x401d00);strcpy((char *)arena+NAME-BASE,"synthetic");}
static HMMIO native_open(void){MMIOINFO info={0};info.fccIOProc=mmioFOURCC('W','A','V',0);info.pIOProc=native_io;return mmioOpenA("synthetic",&info,MMIO_ALLOCBUF);}
int main(int argc,char **argv){
 assert(argc==2 || argc==3);arena=VirtualAlloc(NULL,BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);
 struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,BYTES,&image)==FNV_NATIVE_OK);
 struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=BYTES;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;c.image_bytes=image.size;c.process_id=11;c.peb_address=BASE+0x8000;
 struct fnv_windows_process *process;assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);
 struct fnv_mmio_config cfg={BASE+0x50000,0x11000,NULL};struct fnv_windows_mmio *mmio;assert(fnv_windows_mmio_create(process,&cfg,&mmio)==FNV_CPU_OK);
 size_t n,bound;const struct fnv_cpu_export *exports=fnv_windows_mmio_exports(&n);assert(n==9);struct fnv_cpu_export all[10];memcpy(all,exports,n*sizeof(*exports));all[9]=(struct fnv_cpu_export){"test-mmio.dll","TestIO",0,0,test_io};
 struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,all,10,&bound)==FNV_NATIVE_OK && bound==10);
 struct fnv_windows_thread_config t={0};t.thread_id=22;t.teb_address=BASE+0x12000;t.tls_expansion_address=t.teb_address+4096;t.stack_low=BASE+0x20000;t.stack_high=BASE+0x40000;fnv_cpu_state_init(&t.state,t.stack_high-64,t.teb_address);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
 initial_info();for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*37u+19);
 if(argc==3){uint32_t h=0,args[]={NAME,INFO,0x10000,0},answer=0;struct fnv_cpu_fault f;
  if(!strcmp(argv[2],"--strict-open-mode"))args[2]=0;
  else if(!strcmp(argv[2],"--strict-callback-abi"))put(INFO+8,0x401500+9*32); /* TestIO is not a four-argument forwarding callback? */
  else {h=call(0,NAME,INFO,0x10000,0);assert(h);args[0]=h;args[1]=!strcmp(argv[2],"--strict-output")?BASE+BYTES:INFO;args[2]=0;
    if(!strcmp(argv[2],"--strict-set-info")){assert(!call(4,h,INFO,0,0));put(INFO+24,1);}}
  if(!strcmp(argv[2],"--strict-end-offset")){args[1]=7;args[2]=SEEK_END;}
  unsigned api=!strcmp(argv[2],"--strict-end-offset")?3:!strcmp(argv[2],"--strict-output")?4:!strcmp(argv[2],"--strict-set-info")?5:0;
  if(!strcmp(argv[2],"--strict-callback-abi")){put(INFO+8,0x401500+2*32);} /* wrong callback uses mmioRead with invalid handle then ret12 */
  assert(invoke(api,args,&answer,&f)==FNV_CPU_GUEST_FAULT);puts("strict PASS");return 0;
 }
 uint32_t h=call(0,NAME,INFO,0x10000,0);assert(h);HMMIO native=native_open();assert(native);unsigned checks=0;
 unsigned char native_data[1000];unsigned seed=0x12345;
 for(unsigned i=0;i<1000;i++){seed=seed*1664525u+1013904223u;uint32_t pos=seed%(data_bytes+200);
  assert((int32_t)call(3,h,pos,SEEK_SET,0)==mmioSeek(native,(LONG)pos,SEEK_SET));seed=seed*1664525u+1013904223u;uint32_t n=seed%sizeof(native_data);
  LONG expected=mmioRead(native,(HPSTR)native_data,(LONG)n);uint32_t got=call(2,h,OUT_AT,n,0);assert((int32_t)got==expected && !memcmp(arena+OUT_AT-BASE,native_data,got));
  assert((int32_t)call(3,h,0,SEEK_CUR,0)==mmioSeek(native,0,SEEK_CUR));checks+=3;
 }
 assert(!call(4,h,INFO,0,0));MMIOINFO ni;assert(!mmioGetInfo(native,&ni,0));assert(get(INFO+20)==(uint32_t)ni.cchBuffer && get(INFO+48)==ni.adwInfo[0]);
 assert(!call(1,h,0,0,0) && !mmioClose(native,0));
 initial_info();h=call(0,NAME,INFO,0x10000,0);native=native_open();assert(h && native);
 assert((int32_t)call(3,h,0,SEEK_SET,0)==mmioSeek(native,0,SEEK_SET));
 assert(call(2,h,OUT_AT,1,0)==(uint32_t)mmioRead(native,(HPSTR)native_data,1));
 assert(!call(4,h,INFO,0,0) && !mmioGetInfo(native,&ni,0));
 put(INFO+28,get(INFO+28)+3);ni.pchNext+=3;assert(!call(5,h,INFO,0,0) && !mmioSetInfo(native,&ni,0));
 assert((int32_t)call(3,h,0,SEEK_CUR,0)==mmioSeek(native,0,SEEK_CUR));
 assert((int32_t)call(3,h,0,SEEK_SET,0)==mmioSeek(native,0,SEEK_SET));
 assert(call(2,h,OUT_AT,8192,0)==8192);unsigned char full[8192];assert(mmioRead(native,(HPSTR)full,8192)==8192);
 assert(!call(4,h,INFO,0,0) && !mmioGetInfo(native,&ni,0));
 assert(!call(6,h,INFO,0,0) && !mmioAdvance(native,&ni,0));
 assert(call(2,h,OUT_AT,8,0)==(uint32_t)mmioRead(native,(HPSTR)native_data,8) && !memcmp(arena+OUT_AT-BASE,native_data,8));checks+=8;
 assert((int32_t)call(3,h,0,SEEK_END,0)==mmioSeek(native,0,SEEK_END));
 assert((int32_t)call(3,h,-7,SEEK_CUR,0)==mmioSeek(native,-7,SEEK_CUR));
 uint32_t got=call(2,h,OUT_AT,20,0);LONG expected=mmioRead(native,(HPSTR)native_data,20);assert((int32_t)got==expected && !memcmp(arena+OUT_AT-BASE,native_data,got));checks+=3;
 assert(!call(1,h,0,0,0) && !mmioClose(native,0));assert((int32_t)call(2,h,OUT_AT,1,0)==-1);
 /* Small RIFF with an odd JUNK chunk verifies padding and parent limits. */
 const unsigned char riff[]={'R','I','F','F',40,0,0,0,'W','A','V','E','J','U','N','K',3,0,0,0,1,2,3,0,'f','m','t',' ',4,0,0,0,1,0,2,0,'d','a','t','a',4,0,0,0,9,8,7,6};
 memcpy(data,riff,sizeof(riff));data_bytes=sizeof(riff);initial_info();h=call(0,NAME,INFO,0x10000,0);native=native_open();assert(h && native);
 MMCKINFO parent={0};memset(arena+CK-BASE,0,40);assert(!call(7,h,CK,0,0) && !mmioDescend(native,&parent,NULL,0));
 assert(get(CK)==parent.ckid && get(CK+4)==parent.cksize && get(CK+8)==parent.fccType && get(CK+12)==parent.dwDataOffset);
 MMCKINFO child={0};child.ckid=mmioFOURCC('f','m','t',' ');put(CK+20,child.ckid);assert(!call(7,h,CK+20,CK,MMIO_FINDCHUNK) && !mmioDescend(native,&child,&parent,MMIO_FINDCHUNK));
 assert(get(CK+24)==child.cksize && get(CK+32)==child.dwDataOffset);assert(call(2,h,OUT_AT,4,0)==(uint32_t)mmioRead(native,(HPSTR)native_data,4) && !memcmp(arena+OUT_AT-BASE,native_data,4));
 assert(!call(8,h,CK+20,0,0) && !mmioAscend(native,&child,0));child.ckid=mmioFOURCC('d','a','t','a');put(CK+20,child.ckid);
 assert(!call(7,h,CK+20,CK,MMIO_FINDCHUNK) && !mmioDescend(native,&child,&parent,MMIO_FINDCHUNK));assert(get(CK+32)==child.dwDataOffset);
 assert(!call(1,h,0,0,0) && !mmioClose(native,0));
 initial_info();strcpy((char *)arena+NAME-BASE,"missing");assert(!call(0,NAME,INFO,0x10000,0) && get(INFO+12)==257);
 assert(guest_closes==3 && native_closes==3 && guest_reads && native_reads);
 uint32_t handles[8];initial_info();for(unsigned i=0;i<8;i++){handles[i]=call(0,NAME,INFO,0x10000,0);assert(handles[i]);}
 assert(!call(0,NAME,INFO,0x10000,0) && get(INFO+12)==258);
 for(unsigned i=0;i<8;i++)assert(!call(1,handles[i],0,0,0));
 initial_info();uint32_t replacement=call(0,NAME,INFO,0x10000,0);assert(replacement && replacement!=handles[0]);
 assert((int32_t)call(2,handles[0],OUT_AT,1,0)==-1 && !call(1,replacement,0,0,0));

 assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_mmio_destroy(mmio)==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
 printf("{\"status\":\"PASS\",\"windows_read_seek_comparisons\":%u,\"custom_callbacks_translated\":true,\"riff_padding_verified\":true,\"native_engine_started\":false}\n",checks);return 0;
}
