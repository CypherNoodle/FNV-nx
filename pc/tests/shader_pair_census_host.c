#include "shader_pair_census.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(fnv_shader_fingerprint((const unsigned char*)"",0)==UINT64_C(0xcbf29ce484222325));
    assert(fnv_shader_fingerprint((const unsigned char*)"hello",5)==UINT64_C(0xa430d84680aabd0b));
    struct fnv_shader_pair_census c={0};
    for(unsigned i=0;i<32;i++)fnv_shader_pair_note(&c,i,8,100,12,0,1);
    assert(c.count==32 && !c.overflow);
    fnv_shader_pair_note(&c,31,8,100,12,0,1);assert(c.rows[31].hits==2);
    /* Every identity field matters, including fixed variant and reason. */
    fnv_shader_pair_note(&c,31,9,100,12,0,1);
    fnv_shader_pair_note(&c,31,8,100,13,0,1);
    fnv_shader_pair_note(&c,31,8,100,12,16,1);
    fnv_shader_pair_note(&c,31,8,100,12,0,2);
    fnv_shader_pair_note(&c,31,8,101,12,0,1);
    assert(c.overflow==5 && c.count==32 && c.rows[31].hits==2);
    const unsigned char elements[16]={0,0,0,0,2,0,0,0,0,0,12,0,4,0,10,1};
    struct fnv_shader_pair_record *r=c.rows;
    fnv_shader_pair_attributes(r,elements,2);assert(r->attribute_state==1 && r->attribute_count==2);
    assert(r->attributes[0]==0x020000 && r->attributes[1]==0x04010a);
    fnv_shader_pair_attributes(r,elements,2);assert(r->attribute_state==1);
    fnv_shader_pair_attributes(r,elements,1);assert(r->attribute_state==3);
    fnv_shader_pair_attributes(r,elements,2);assert(r->attribute_state==3);
    fnv_shader_pair_attributes(c.rows+1,elements,17);assert(c.rows[1].attribute_state==2);
    fnv_shader_pair_attributes(0,elements,2);
    puts("PASS: fingerprint vectors, repeated hits, bounded capacity, overflow, identity fields");
    return 0;
}
