#include "bink_copy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
int main(void){
    unsigned char source[48]={255,0,0,255,0,255,0,1,0,0,255,127,0,0,0,0,
        1,2,3,255,11,22,33,0,111,122,133,99,0,0,0,0,
        255,255,255,255,0,0,0,0,32,64,128,17,0,0,0,0};
    struct fnv_bink_frame f={0};f.rgba=source;f.width=3;f.height=3;f.pitch=16;
    unsigned char dest[160],snapshot[160];unsigned checks=0;
    for(unsigned format=3;format<=4;format++)for(unsigned flags=0;flags<4;flags++){
        memset(dest,0xa5,sizeof(dest));uint32_t mode=format|((flags&1)?0x80000000u:0)|((flags&2)?0x70000000u:0);
        assert(fnv_bink_copy_rect(&f,sizeof(source),dest,sizeof(dest),32,5,2,1,0,0,3,3,mode)==FNV_BINK_COPY_OK);
        for(unsigned i=0;i<sizeof(dest);i++){
            unsigned y=i/32,x=i%32;int touched=y>=1&&y<4&&x>=8&&x<20;
            unsigned char expected=0xa5;
            if(touched){unsigned channel=(x-8)%4,at=(y-1)*16+(x-8)/4*4;
                expected=channel==3?0:source[at+(format==3?2-channel:channel)];}
            assert(dest[i]==expected);checks++;
        }
    }
    memset(dest,0xa5,sizeof(dest));
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,1,2,1,1,2,2,3)==FNV_BINK_COPY_OK);
    const unsigned char cropped[16]={33,22,11,0,133,122,111,0,0,0,0,0,128,64,32,0};
    assert(!memcmp(dest+68,cropped,8)&&!memcmp(dest+100,cropped+8,8));
    for(unsigned i=0;i<160;i++)if(!((i>=68&&i<76)||(i>=100&&i<108)))assert(dest[i]==0xa5);
    for(unsigned format=3;format<=4;format++){
        memset(dest,0xa5,sizeof(dest));
        assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,1,2,1,1,256,256,format)==FNV_BINK_COPY_OK);
        for(unsigned i=0;i<160;i++){
            unsigned y=i/32,x=i%32;unsigned char expected=0xa5;
            if(y>=2&&y<4&&x>=4&&x<12){unsigned c=(x-4)%4;
                expected=c==3?0:source[(y-1)*16+(1+(x-4)/4)*4+(format==3?2-c:c)];}
            assert(dest[i]==expected);checks++;
        }
    }
    memset(dest,0x6d,sizeof(dest));memcpy(snapshot,dest,sizeof(dest));
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,0,0,0,0,3,3,5)==FNV_BINK_COPY_UNSUPPORTED);
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,0,0,0,0,3,3,0x10000003)==FNV_BINK_COPY_UNSUPPORTED);
    assert(fnv_bink_copy_rect(&f,48,dest,160,-32,5,0,0,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,43,dest,160,32,5,0,0,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,48,dest,159,32,5,0,0,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,UINT32_MAX,0,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,0,0,UINT32_MAX,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,0,4,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(fnv_bink_copy_rect(&f,48,dest,160,32,5,0,0,0,0,0,3,3)==FNV_BINK_COPY_INVALID);
    assert(!memcmp(dest,snapshot,sizeof(dest)));
    unsigned char original[48];memcpy(original,source,48);
    assert(fnv_bink_copy_rect(&f,48,source,48,16,3,0,0,0,0,3,3,3)==FNV_BINK_COPY_INVALID);
    assert(!memcmp(original,source,48));
    printf("{\"status\":\"PASS\",\"guard_and_pixel_bytes\":%u,\"rejected_cases\":10,\"game_started\":false}\n",checks);return 0;
}
