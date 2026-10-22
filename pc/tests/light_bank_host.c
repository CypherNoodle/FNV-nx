#include <stdlib.h>
static int allocations_before_failure=-1;
static void *fault_realloc(void *p,size_t n){
    if(!allocations_before_failure)return NULL;
    if(allocations_before_failure>0)allocations_before_failure--;
    return realloc(p,n);
}
#define realloc fault_realloc
#include "d3d9_light_state.h"
#undef realloc
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
int main(void){
    struct fnv_light_bank live={0},record={0};uint32_t on=1,off=0,words[26]={3};
    allocations_before_failure=1;
    assert(fnv_light_change(&live,&record,UINT32_MAX,words,&on)==0x8876017cu);
    assert(!live.count && !record.count);allocations_before_failure=-1;
    assert(!fnv_light_change(&live,&record,UINT32_MAX,words,&on));
    assert(live.count==1 && fnv_light_find(&live,UINT32_MAX)->words[1]==0x3f800000 && !fnv_light_find(&live,UINT32_MAX)->enabled);
    assert(!fnv_light_block_prepare(&live,&record,0));fnv_light_block_commit(&live,&record,0);
    assert(fnv_light_find(&live,UINT32_MAX)->words[1]==0 && fnv_light_find(&live,UINT32_MAX)->enabled==128);
    fnv_light_destroy(&record);assert(!fnv_light_change(&live,&record,UINT32_MAX,NULL,&off));
    words[1]=0x40000000;assert(!fnv_light_change(&live,NULL,UINT32_MAX,words,NULL));
    assert(!fnv_light_block_prepare(&live,&record,0));fnv_light_block_commit(&live,&record,0);
    assert(fnv_light_find(&live,UINT32_MAX)->words[1]==0x40000000 && !fnv_light_find(&live,UINT32_MAX)->enabled);
    live.count=0;assert(fnv_light_block_prepare(&live,&record,1)==0x8876086cu);
    assert(!fnv_light_block_prepare(&live,&record,0));fnv_light_block_commit(&live,&record,0);
    assert(fnv_light_find(&live,UINT32_MAX)->words[1]==0x3f800000);
    fnv_light_destroy(&record);fnv_light_destroy(&live);
    /* Index magnitude does not control allocation; the number of entries does. */
    for(unsigned i=0;i<4096;i++)assert(!fnv_light_change(&live,NULL,UINT32_MAX-i,words,NULL));
    assert(live.count==4096 && fnv_light_change(&live,NULL,1,words,NULL)==0x8876017cu && live.count==4096);
    for(unsigned i=0;i<4096;i++)assert(fnv_light_find(&live,UINT32_MAX-i));
    fnv_light_destroy(&live);puts("PASS sparse indices, allocation rollback, field masks, budget and reset recreation");return 0;
}
