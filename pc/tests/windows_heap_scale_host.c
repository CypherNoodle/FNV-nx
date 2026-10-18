#define ARENA 0x2000000u
#define HEAP_BYTES 0x1000000u
#define main small_heap_regression_main
#include "windows_heap_host.c"
#undef main
struct model_block {uint32_t address,size,span,tag;};
static struct model_block model[1024];static unsigned model_count;
static unsigned model_find(uint32_t p){for(unsigned i=0;i<model_count;++i)if(model[i].address==p)return i;abort();}
static uint32_t model_allocate(uint32_t bytes){
    uint32_t span=bytes?((bytes+7)&~7u):8,at=HEAP_BASE+8;unsigned i=0;
    for(;i<model_count;++i){if(at+span<=model[i].address)break;at=model[i].address+model[i].span;}
    assert(model_count<1024);memmove(model+i+1,model+i,(model_count-i)*sizeof(*model));model[i]=(struct model_block){at,bytes,span,0};model_count++;return at;
}
static void model_free(uint32_t p){unsigned i=model_find(p);memmove(model+i,model+i+1,(model_count-i-1)*sizeof(*model));model_count--;}
static uint32_t model_resize(uint32_t p,uint32_t bytes){
    unsigned i=model_find(p);uint32_t span=bytes?((bytes+7)&~7u):8,end=i+1<model_count?model[i+1].address:HEAP_BASE+HEAP_BYTES;
    if(p+span<=end){model[i].size=bytes;model[i].span=span;return p;}
    uint32_t next=model_allocate(bytes);model_free(p);return next;
}
static double dense(unsigned count,uint32_t *pointers){
    LARGE_INTEGER start,end,hz;assert(QueryPerformanceFrequency(&hz) && QueryPerformanceCounter(&start));
    for(unsigned i=0;i<count;++i){pointers[i]=call(ALLOC,HEAP_BASE,0,48,0);assert(pointers[i]==HEAP_BASE+8+i*48);}
    assert(QueryPerformanceCounter(&end));double ms=(end.QuadPart-start.QuadPart)*1000.0/hz.QuadPart;
    for(unsigned i=0;i<count;++i)assert(call(HEAP_SIZE,HEAP_BASE,0,pointers[i],0)==48);
    for(unsigned i=count;i;--i)assert(call(FREE,HEAP_BASE,0,pointers[i-1],0));
    return ms;
}
int main(int argc,char **argv){
    assert(argc==2);arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config cfg={0};cfg.cpu.memory=arena;cfg.cpu.guest_base=BASE;cfg.cpu.memory_bytes=ARENA;cfg.cpu.code_low=BASE+0x1000;cfg.cpu.code_high=BASE+image.size;
    cfg.image_bytes=image.size;cfg.process_id=11;cfg.peb_address=BASE+0x8000;cfg.heap_address=HEAP_BASE;cfg.heap_bytes=HEAP_BYTES;cfg.heap_capacity=220000;
    assert(fnv_windows_create(&cfg,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==8);
    struct fnv_windows_thread_config t=thread_config(0);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    struct fnv_windows_heap_stats stats;assert(fnv_windows_heap_stats(process,&stats)==FNV_CPU_OK && stats.metadata_bytes==384);
    uint32_t *pointers=malloc(200000*sizeof(*pointers));assert(pointers);double first=dense(100000,pointers),second=dense(200000,pointers);free(pointers);
    assert(fnv_windows_heap_stats(process,&stats)==FNV_CPU_OK && !stats.live_blocks && !stats.allocation_steps && !stats.gap_steps && stats.lookup_steps<12000000);
    uint64_t dense_searches=stats.lookup_steps;
    unsigned seed=0x583A31;
    for(unsigned n=0;n<4000;++n){seed=seed*1664525u+1013904223u;unsigned op=seed%3;
        if(!model_count || (op==0 && model_count<1000)){
            unsigned bytes=(seed>>8)%512;uint32_t expected=model_allocate(bytes),actual=call(ALLOC,HEAP_BASE,8,bytes,0);assert(actual==expected);
            for(unsigned i=0;i<bytes;++i)assert(!arena[actual-BASE+i]);unsigned index=model_find(actual);model[index].tag=n&255;memset(arena+actual-BASE,n&255,bytes);
        }else{unsigned index=(seed>>16)%model_count;struct model_block old=model[index];
            for(unsigned i=0;i<old.size;++i)assert(arena[old.address-BASE+i]==old.tag);
            if(op==1){assert(call(FREE,HEAP_BASE,0,old.address,0));model_free(old.address);}
            else{unsigned bytes=(seed>>6)%1024;uint32_t expected=model_resize(old.address,bytes),actual=call(REALLOC,HEAP_BASE,8,old.address,bytes);assert(actual==expected);
                for(unsigned i=0;i<bytes;++i)assert(arena[actual-BASE+i]==(i<old.size?old.tag:0));
                index=model_find(actual);model[index].tag=n&255;memset(arena+actual-BASE,n&255,bytes);}
        }
    }
    while(model_count){uint32_t p=model[model_count-1].address;assert(call(FREE,HEAP_BASE,0,p,0));model_free(p);}
    assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"dense_allocations\":300000,\"first_fit_model_operations\":4000,\"dense_allocation_scans\":0,\"lookup_comparisons\":%llu,\"alloc_100k_ms\":%.3f,\"alloc_200k_ms\":%.3f,\"initial_metadata_bytes\":384,\"native_engine_started\":false}\n",(unsigned long long)dense_searches,first,second);return 0;
}
