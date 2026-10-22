#ifndef FNV_CATALOG_MIP_FIXTURE_H
#define FNV_CATALOG_MIP_FIXTURE_H
static const unsigned mip_clamp[6]={0,1,2,3,1,3};
static const unsigned mip_filter[6]={0,0,1,1,2,2};
static void mip_texels(unsigned level,unsigned char *data){
    const unsigned char color[4][4]={{23,67,211,193},{173,47,109,127},{71,229,53,89},{251,139,17,241}};
    unsigned width=8u>>level;
    for(unsigned i=0;i<width*width;i++)memcpy(data+i*4,color[level],4);
}
#endif
