/* Real backend cubemap lookup of own solid colors on all six axes. */
#include "d3d9_vk_backend.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void *load(const char *name,uint32_t *bytes){FILE *f=fopen(name,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<8||n>1048576){fclose(f);return NULL;}void *p=malloc((size_t)n);if(!p||fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}fclose(f);*bytes=(uint32_t)n;return p;}
int main(int argc,char **argv){
 CHECK(argc==7);HMODULE loader=LoadLibraryExW(L"vulkan-1.dll",NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(loader);FARPROC raw=GetProcAddress(loader,"vkGetInstanceProcAddr");PFN_vkGetInstanceProcAddr gpa;memcpy(&gpa,&raw,sizeof(gpa));
 struct fnv_vk_config cfg={16,16,4096,3,32,64u<<20,0};struct fnv_vk_backend *d=NULL;VkResult why;
 CHECK(!fnv_vk_backend_create(gpa,&cfg,&d,&why));struct fnv_d3d9_shader_backend p;fnv_vk_backend_callbacks(d,&p);
 void *code[6];uint32_t bytes[6];for(unsigned i=0;i<6;i++){code[i]=load(argv[i+1],bytes+i);CHECK(code[i]);}
 struct fnv_uniform_entry entry={0,0,1,0,16};struct fnv_uniform_plan plans[2]={{0,0,NULL},{16,1,&entry}};
 for(unsigned i=0;i<2;i++)CHECK(!fnv_vk_backend_register_shader(d,(enum fnv_d3d9_stage)i,code[i],bytes[i],code[i+2],bytes[i+2],plans+i));
 struct fnv_vk_vertex_input input={0,0,0};CHECK(!fnv_vk_backend_link_program(d,code[0],bytes[0],code[1],bytes[1],&input,1,1));
 uintptr_t shaders[2];for(unsigned i=0;i<2;i++){CHECK(!p.create(p.context,(enum fnv_d3d9_stage)i,code[i],bytes[i],shaders+i));CHECK(!p.bind_shader(p.context,(enum fnv_d3d9_stage)i,shaders[i]));}
 const unsigned char decl[]={0,0,0,0,1,0,0,0,255,0,0,0,17,0,0,0};uintptr_t declaration,vb,cube;
 CHECK(!p.create_declaration(p.context,decl,2,&declaration));CHECK(!p.bind_declaration(p.context,declaration));
 const float vertices[]={-1,-1,3,-1,-1,3};CHECK(!p.create_buffer(p.context,FNV_D3D9_VB,sizeof(vertices),0,0,1,&vb));CHECK(!p.update_buffer(p.context,vb,0,sizeof(vertices),0,vertices));CHECK(!p.bind_stream(p.context,0,vb,0,8));
 CHECK(!p.create_cube_texture(p.context,2,1,0,21,1,&cube));
 const unsigned char colors[6][4]={{31,67,101,255},{47,83,127,255},{61,97,149,255},{79,113,163,255},{89,139,181,255},{109,157,199,255}};
 for(unsigned face=0;face<6;face++){unsigned char texels[16];for(unsigned k=0;k<4;k++)memcpy(texels+k*4,colors[face],4);CHECK(!p.upload_cube_texture(p.context,cube,face,0,0,0,2,2,0,8,texels));}
 CHECK(!p.bind_texture(p.context,0,cube));uint32_t values[6]={0};CHECK(!p.state(p.context,FNV_D3D9_RENDER_STATE,0,7,values,0));values[0]=1;CHECK(!p.state(p.context,FNV_D3D9_RENDER_STATE,0,22,values,0));
 uintptr_t target;CHECK(!p.get_target(p.context,0,0,&target));
 const float dirs[6][4]={{1,0,0,0},{-1,0,0,0},{0,1,0,0},{0,-1,0,0},{0,0,1,0},{0,0,-1,0}};unsigned checked=0;
 for(unsigned face=0;face<7;face++){
  if(face==6)CHECK(!p.bind_texture(p.context,0,0));
  CHECK(p.upload_constants(p.context,FNV_D3D9_PIXEL,FNV_D3D9_FLOAT,0,1,(const uint32_t*)dirs[face%6]));
  CHECK(!p.scene(p.context,1));uint32_t hr=p.draw(p.context,0,4,0,0,0,0,1);if(hr)fprintf(stderr,"draw hr=%08x\n",hr);CHECK(!hr);CHECK(!p.scene(p.context,0));
  unsigned char out[16*16*4];CHECK(!fnv_vk_backend_read_color(d,target,out,sizeof(out),1000000000ull));
  for(unsigned i=0;i<16*16;i++){unsigned char expected[4]={0,0,0,255};if(face<6)memcpy(expected,colors[face],4);CHECK(!memcmp(out+i*4,expected,4));checked++;}
 }
 /* Windows accepts a type mismatch and samples transparent black; NULL is opaque. */
 uintptr_t flat;CHECK(!p.create_texture(p.context,1,1,1,0,21,1,&flat));CHECK(!p.upload_texture(p.context,flat,0,0,0,1,1,0,4,colors[0]));CHECK(!p.bind_texture(p.context,0,flat));
 CHECK(!fnv_vk_backend_register_shader(d,FNV_D3D9_PIXEL,code[4],bytes[4],code[5],bytes[5],plans+1));
 CHECK(!fnv_vk_backend_link_program(d,code[0],bytes[0],code[4],bytes[4],&input,1,1));uintptr_t flat_shader;
 CHECK(!p.create(p.context,FNV_D3D9_PIXEL,code[4],bytes[4],&flat_shader));
 for(unsigned which=0;which<4;which++){
  if(which==1){CHECK(!p.bind_shader(p.context,FNV_D3D9_PIXEL,flat_shader));CHECK(!p.bind_texture(p.context,0,cube));}
  if(which==2)CHECK(!p.bind_texture(p.context,0,0));
  if(which==3)CHECK(!p.bind_texture(p.context,0,flat));
  CHECK(!p.scene(p.context,1));CHECK(!p.draw(p.context,0,4,0,0,0,0,1));CHECK(!p.scene(p.context,0));
  unsigned char out[16*16*4];CHECK(!fnv_vk_backend_read_color(d,target,out,sizeof out,1000000000ull));
  unsigned char expected[4]={0};if(which==2)expected[3]=255;if(which==3)memcpy(expected,colors[0],4);
  for(unsigned i=0;i<16*16;i++){CHECK(!memcmp(out+i*4,expected,4));checked++;}
 }
 p.release(p.context,flat_shader);
 CHECK(!p.bind_texture(p.context,0,0));p.release(p.context,flat);p.release(p.context,cube);p.release(p.context,target);p.release(p.context,vb);p.release(p.context,declaration);for(unsigned i=0;i<2;i++)p.release(p.context,shaders[i]);
 uintptr_t many[2050];for(unsigned i=0;i<2050;i++)CHECK(!p.create_buffer(p.context,FNV_D3D9_VB,16,0,0,1,many+i));
 for(unsigned i=0;i<2050;i++)p.release(p.context,many[i]);
 p.device_release(p.context);CHECK(!fnv_vk_backend_wait(d,1000000000ull));CHECK(!fnv_vk_backend_destroy(d));for(unsigned i=0;i<6;i++)free(code[i]);FreeLibrary(loader);
 printf("{\"status\":\"PASS\",\"cube_axis_and_null_pixels\":%u,\"draws\":11,\"resource_capacity_probe\":2050,\"gpu_rendered\":true,\"switch_hardware_tested\":false}\n",checked);return 0;
}
