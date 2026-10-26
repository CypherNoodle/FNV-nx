/* Include the actual backend so the lookup and pin paths are exercised. */
#include "d3d9_vk_backend.c"
#include <stdio.h>
#include <time.h>
#undef NDEBUG
#include <assert.h>
static uint32_t random_state=0x359126ab;
static uint32_t next_random(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void *slot(struct fnv_vk_backend *d,unsigned kind,unsigned index){
    switch(kind){case 0:return d->surfaces+index;case 1:return d->buffer_resources+index;
    case 2:return d->textures+index;case 3:return d->shaders+index;default:return d->declarations+index;}
}
static void *query(struct fnv_vk_backend *d,unsigned kind,uintptr_t id){
    switch(kind){case 0:return find(d,id);case 1:return find_buffer(d,id);
    case 2:return find_texture(d,id);case 3:return find_shader(d,id);default:return find_declaration(d,id);}
}
static void *reference(struct fnv_vk_backend *d,unsigned kind,uintptr_t id){
    if(id)for(unsigned i=0;i<d->config.resource_capacity;i++){void *p=slot(d,kind,i);if(*(uintptr_t*)p==id)return p;}
    return NULL;
}
int main(void){
    struct fnv_vk_backend *d=calloc(1,sizeof(*d));assert(d);d->config.resource_capacity=8192;
    d->surfaces=calloc(8192,sizeof(*d->surfaces));d->buffer_resources=calloc(8192,sizeof(*d->buffer_resources));
    d->textures=calloc(8192,sizeof(*d->textures));d->shaders=calloc(8192,sizeof(*d->shaders));
    d->declarations=calloc(8192,sizeof(*d->declarations));
    assert(d->surfaces && d->buffer_resources && d->textures && d->shaders && d->declarations);
    uintptr_t serial=1;unsigned checks=0;
    for(unsigned kind=0;kind<5;kind++)for(unsigned i=0;i<256;i++){
        void *p=slot(d,kind,8191-i);*(uintptr_t*)p=serial++;
    }
    for(unsigned n=0;n<3000;n++){
        unsigned kind=next_random()%5,index=8191-next_random()%256;
        void *p=slot(d,kind,index);uintptr_t previous=*(uintptr_t*)p;
        if(n%3==0){
            assert(query(d,kind,previous)==reference(d,kind,previous));checks++;
            *(uintptr_t*)p=0;
            assert(!query(d,kind,previous));checks++;
            *(uintptr_t*)p=serial++;
            assert(!query(d,kind,previous));checks++;
        }
        uintptr_t id=*(uintptr_t*)p;
        for(unsigned requested=0;requested<5;requested++){
            assert(query(d,requested,id)==reference(d,requested,id));checks++;
            assert(!query(d,requested,0));assert(!query(d,requested,serial+128));checks+=2;
        }
    }
    /* Keep refs alive while testing the actual cross-kind pin dispatcher. */
    d->surfaces[8191].refs=d->buffer_resources[8191].refs=d->textures[8191].refs=1;
    d->shaders[8191].refs=d->declarations[8191].refs=1;
    for(unsigned kind=0;kind<5;kind++){
        uintptr_t id=*(uintptr_t*)slot(d,kind,8191);pin(d,id,1);pin(d,id,1);pin(d,id,0);pin(d,id,0);
    }
    assert(!d->surfaces[8191].pins && !d->buffer_resources[8191].pins && !d->textures[8191].pins && !d->shaders[8191].pins && !d->declarations[8191].pins);
    /* Serial IDs normally leave these five entries in distinct cache buckets. */
    uintptr_t ids[5];for(unsigned kind=0;kind<5;kind++){ids[kind]=serial++;*(uintptr_t*)slot(d,kind,8191)=ids[kind];}
    volatile uintptr_t sink=0;clock_t start=clock();
    for(unsigned n=0;n<20000;n++)for(unsigned kind=0;kind<5;kind++)for(unsigned requested=0;requested<5;requested++)sink^=(uintptr_t)query(d,requested,ids[kind]);
    double seconds=(double)(clock()-start)/CLOCKS_PER_SEC;
    printf("{\"status\":\"PASS\",\"comparisons\":%u,\"capacity\":8192,\"lookup_seconds\":%.6f,\"benchmark_queries\":500000,\"sink\":%llu}\n",checks,seconds,(unsigned long long)sink);
    free(d->surfaces);free(d->buffer_resources);free(d->textures);free(d->shaders);free(d->declarations);free(d);return 0;
}
