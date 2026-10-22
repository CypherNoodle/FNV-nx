/* Original synthetic x86 callers -> COM -> one Vulkan device, three fixed PS. */
#define main unused_nullref_baseline
#include "d3d9_com_host.c"
#undef main
#include "d3d9_vk_backend.h"
#include "d3d9_directional_vertex.h"
#include "fixed_switch_fixture.h"
#define FNV_DIRECTIONAL_TEXTURED 1
#define FNV_FIXED_SWITCH 1
#include "directional_vertex_oracle.h"
enum {X_PS=20,X_FVF,X_VB,X_LOCK,X_UNLOCK,X_STREAM,X_RENDER,X_BEGIN,X_END,X_DRAW,X_MATRIX,X_MATERIAL,X_LIGHT,X_ENABLE,
 X_TEXTURE,X_TLOCK,X_TUNLOCK,X_TBIND,X_STAGE,X_GET_STAGE,X_GET_PS,X_PARENT,X_CREATE,X_RESET,X_BLOCK_BEGIN,X_BLOCK_END,X_CAPTURE,X_APPLY,X_VB_DESC,X_GET_FVF};
static unsigned char *file_bytes(const char *root,const char *name,uint32_t *size){
 char path[2048];int n=snprintf(path,sizeof(path),"%s/%s",root,name);assert(n>0 && (size_t)n<sizeof(path));FILE *f=fopen(path,"rb");assert(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes>0 && bytes<1048576);rewind(f);
 unsigned char *p=malloc((size_t)bytes);assert(p && fread(p,1,(size_t)bytes,f)==(size_t)bytes && !fclose(f));*size=(uint32_t)bytes;return p;
}
static void stage_mode(unsigned mode){
 const uint32_t state[3][6]={{2,0,1,2,0,1},{4,2,1,4,2,1},{4,2,1,2,2,1}};assert(mode<3);
 for(unsigned i=0;i<6;i++){uint32_t a[]={device_at,0,i+1,state[mode][i]};assert(!call(X_STAGE,4,a));}
}
static void texture_upload(uint32_t texture){
 uint32_t a[]={texture,0,GUEST_OUT,0,0};assert(!call(X_TLOCK,5,a));uint32_t pitch=get(GUEST_OUT);unsigned char *p=ptr(get(GUEST_OUT+4));
 for(unsigned y=0;y<2;y++)memcpy(p+y*pitch,directional_texels+y*8,8);uint32_t unlock[]={texture,0};assert(!call(X_TUNLOCK,2,unlock));
}
static const uint32_t present[14]={32,32,21,1,0,0,1,1,1,0,0,0,0,1};
int main(int argc,char **argv){
 assert(argc==5);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);assert(loader);FARPROC proc=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gp;memcpy(&gp,&proc,sizeof(gp));
 struct fnv_vk_config cfg={32,32,32,2,16,16u<<20,0};struct fnv_vk_backend *gpu;VkResult failure;assert(!fnv_vk_backend_create(gp,&cfg,&gpu,&failure));
 struct fnv_d3d9_shader_backend b;fnv_vk_backend_callbacks(gpu,&b);unsigned char *code[4];uint32_t sizes[4];uintptr_t prepared[4];
 const unsigned buffer_formats[6]={0,0x102,0x112,0x152,0x104,0x4002};
 for(unsigned i=0;i<6;i++){uintptr_t id;assert(!b.create_buffer(gpu,FNV_D3D9_VB,257,0,buffer_formats[i],1,&id));b.release(gpu,id);}
 uintptr_t untouched=123;struct fnv_vk_stats before,after;assert(!fnv_vk_backend_stats(gpu,&before));
 assert(b.create_buffer(gpu,FNV_D3D9_VB,257,0,1,1,&untouched)==FNV_VK_INVALID && untouched==123);
 assert(!fnv_vk_backend_stats(gpu,&after) && before.allocated_bytes==after.allocated_bytes);
 catalog_count=4;catalog=calloc(4,sizeof(*catalog));assert(catalog);
 for(unsigned i=0;i<4;i++){
  char name[32];if(!i)strcpy(name,"vs.bin");else snprintf(name,sizeof(name),"ps%u.bin",i-1);code[i]=file_bytes(argv[4],name,sizes+i);catalog[i]=(struct fnv_d3d9_shader_code){code[i],sizes[i]};
  if(!i)strcpy(name,"pair1/vs.spv");else snprintf(name,sizeof(name),"pair%u/ps.spv",i-1);
  uint32_t bytes;unsigned char *spv=file_bytes(argv[4],name,&bytes);struct fnv_uniform_plan plan=i?(struct fnv_uniform_plan){0}:vertex_plan;
  assert(!fnv_vk_backend_register_shader(gpu,i?1:0,code[i],sizes[i],spv,bytes,&plan));free(spv);assert(!b.create(gpu,i?1:0,code[i],sizes[i],prepared+i));
 }
 assert(!fnv_vk_backend_fixed_directional(gpu,prepared[0]));
 for(unsigned m=0;m<3;m++){
  assert(!fnv_vk_backend_link_program(gpu,code[0],sizes[0],code[m+1],sizes[m+1],vertex_inputs,3,m?1:0));
  assert(!fnv_vk_backend_fixed_pixel(gpu,m,prepared[(!strcmp(argv[3],"wrong-alpha") && m==2)?2:m+1]));
 }
 for(unsigned i=0;i<4;i++)b.release(gpu,prepared[i]); /* only internal pins remain */
 arena=VirtualAlloc(NULL,ARENA,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(arena);struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,ARENA,&image));
 struct fnv_cpu_config cp={0};cp.memory=arena;cp.guest_base=BASE;cp.memory_bytes=ARENA;cp.code_low=BASE+0x1000;cp.code_high=BASE+0x3000;assert(!fnv_cpu_create(&cp,&cpu));code_at=BASE+0x200000;
 struct fnv_d3d9_factory_profile profile={32,32,21,0,60,gpu,NULL};
 struct fnv_d3d9_com_config cc={.objects_address=BASE+0x18000,.objects_bytes=4096,.resource_capacity=32,.vertex_floats=256,.pixel_floats=224,.backend=b,.catalog=catalog,.catalog_count=4,
  .buffer_address=BASE+0x1000000,.buffer_bytes=4096,.factory=&profile,.reset_timeout_ns=2000000000ull};assert(!fnv_d3d9_com_create(cpu,&cc,&com));device_at=fnv_d3d9_com_device(com);
 struct fnv_cpu_state initial;fnv_cpu_state_init(&initial,BASE+ARENA-4096,0);assert(!fnv_cpu_attach(cpu,&initial));
 uint32_t parent[]={device_at,GUEST_OUT};assert(!call(X_PARENT,2,parent));uint32_t factory=get(GUEST_OUT);memcpy(ptr(DATA),present,56);
 uint32_t create_device[]={factory,0,1,1,64,DATA,GUEST_OUT};assert(!call(X_CREATE,7,create_device) && get(GUEST_OUT)==device_at);assert(release(factory)==1);
 uint32_t vs=make_shader(0),ps[3];for(unsigned m=0;m<3;m++)ps[m]=make_shader(m+1);
 const float positions[18]={-1,1,.5f,1,1,.5f,1,-1,.5f,-1,1,.5f,1,-1,.5f,-1,-1,.5f};float vertices[48]={0};
 for(unsigned i=0;i<6;i++){memcpy(vertices+i*8,positions+i*3,12);vertices[i*8+3]=1;vertices[i*8+5]=-1;}directional_uv(4,vertices);
 uint32_t create_vb[]={device_at,sizeof(vertices),0,0x112,1,GUEST_OUT,0};assert(!call(X_VB,7,create_vb));uint32_t vb=get(GUEST_OUT);
 uint32_t desc[]={vb,DATA};assert(!call(X_VB_DESC,2,desc) && get(DATA+16)==sizeof(vertices) && get(DATA+20)==0x112);
 uint32_t current[]={device_at,GUEST_OUT};assert(!call(X_GET_FVF,2,current) && !get(GUEST_OUT));
 uint32_t lock[]={vb,0,sizeof(vertices),GUEST_OUT,0};assert(!call(X_LOCK,5,lock));memcpy(ptr(get(GUEST_OUT)),vertices,sizeof(vertices));assert(!call(X_UNLOCK,1,&vb));
 uint32_t create_texture[]={device_at,2,2,1,0,21,1,GUEST_OUT,0};assert(!call(X_TEXTURE,9,create_texture));uint32_t texture=get(GUEST_OUT);texture_upload(texture);assert(!fnv_vk_backend_wait(gpu,10000000000ull));
 unsigned sequence[9]={0,1,2,1,0,2,1,0,2},checks=0,resets=0;
 for(unsigned frame=0;frame<9;frame++){
  if(frame==6){memcpy(ptr(DATA),present,56);uint32_t reset[]={device_at,DATA};assert(!call(X_RESET,2,reset));resets++;}
  unsigned mode=sequence[frame];int programmable=frame==3 || frame==7;
  uint32_t fvf[]={device_at,0x112},stream[]={device_at,0,vb,0,32},bind_texture[]={device_at,0,texture};assert(!call(X_FVF,2,fvf) && !call(X_STREAM,5,stream) && !call(X_TBIND,3,bind_texture));
  const uint32_t render_states[5][2]={{22,1},{7,0},{137,1},{141,0},{143,1}};
  for(unsigned i=0;i<5;i++){uint32_t a[]={device_at,render_states[i][0],render_states[i][1]};assert(!call(X_RENDER,3,a));}
  struct fnv_transform_state bank;fnv_transform_init(&bank);float material[17]={.5f,.25f,.75f,.6f},light[26]={0};light[1]=light[2]=light[3]=1;light[18]=1;uint32_t mat[17],words[26],on=1;memcpy(mat,material,68);memcpy(words,light,104);words[0]=3;
  memcpy(ptr(DATA),mat,68);uint32_t setmat[]={device_at,DATA};assert(!call(X_MATERIAL,2,setmat));memcpy(ptr(DATA),words,104);uint32_t setlight[]={device_at,0,DATA},enable[]={device_at,0,1};assert(!call(X_LIGHT,3,setlight) && !call(X_ENABLE,3,enable));
  if(!frame || frame==6){struct fnv_light_bank lights={0};assert(!fnv_light_change(&lights,NULL,0,words,&on));float constants[40];assert(!fnv_fixed_directional_columns(&bank,mat,&lights,0,1,constants));fnv_light_destroy(&lights);memcpy(ptr(DATA),constants,160);uint32_t upload[]={device_at,0,DATA,10};assert(!call(VS_SF,4,upload));}
  stage_mode(mode);
  if(frame==4){
   stage_mode(1);assert(!call(X_BLOCK_BEGIN,1,&device_at));stage_mode(0);uint32_t finish[]={device_at,GUEST_OUT};assert(!call(X_BLOCK_END,2,finish));uint32_t block=get(GUEST_OUT);
   uint32_t before[]={device_at,0,1,GUEST_OUT};assert(!call(X_GET_STAGE,4,before) && get(GUEST_OUT)==4);
   if(strcmp(argv[3],"wrong-stateblock"))assert(!call(X_APPLY,1,&block));assert(!release(block));
  }
  if(frame==2 && !strcmp(argv[3],"wrong-selector"))stage_mode(1);
  if(programmable){uint32_t unsupported[]={device_at,0,1,99};assert(!call(X_STAGE,4,unsupported));}
  uint32_t bindvs[]={device_at,programmable?vs:0},bindps[]={device_at,programmable?ps[mode]:0};assert(!call(UNSUPPORTED,2,bindvs) && !call(X_PS,2,bindps));
  oracle_mode=mode;unsigned char expected[4096];oracle(0,vertices,&bank,mat,words,0,expected);
  assert(!b.clear(gpu,0,NULL,1,0xff000000,0,0));assert(!fnv_vk_backend_flush(gpu) && !fnv_vk_backend_wait(gpu,10000000000ull));
  assert(!call(X_BEGIN,1,&device_at));uint32_t draw[]={device_at,4,0,2};assert(!call(X_DRAW,4,draw) && !call(X_END,1,&device_at));
  uint32_t getps[]={device_at,GUEST_OUT};assert(!call(X_GET_PS,2,getps));uint32_t observed=get(GUEST_OUT);assert(observed==(programmable?ps[mode]:0));if(observed)assert(release(observed)==1);
  uintptr_t color;assert(!b.get_target(gpu,0,0,&color));unsigned char actual[4096];assert(!fnv_vk_backend_read_color(gpu,color,actual,4096,10000000000ull));b.release(gpu,color);
  for(unsigned i=0;i<4096;i++){if(abs((int)actual[i]-expected[i])>1){fprintf(stderr,"switch pixel mismatch frame=%u byte=%u actual=%u expected=%u\n",frame,i,actual[i],expected[i]);return 1;}if(i%4==0)checks++;}
 }
 assert(!release(vs));for(unsigned m=0;m<3;m++)assert(!release(ps[m]));assert(!release(vb) && !release(texture));assert(!fnv_cpu_detach() && !fnv_d3d9_com_destroy(com));assert(!fnv_cpu_destroy(cpu) && VirtualFree(arena,0,MEM_RELEASE));assert(!fnv_vk_backend_destroy(gpu));for(unsigned i=0;i<4;i++)free(code[i]);free(catalog);FreeLibrary(loader);
 printf("{\"status\":\"PASS\",\"gpu_rendered\":true,\"pixel_checks\":%u,\"successful_resets\":%u,\"same_device_modes\":3,\"game_frame_rendered\":false}\n",checks,resets);return 0;
}
