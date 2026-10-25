/* Synthetic six-face uploads; no game DDS, shaders or media. */
static int cube_textures(struct fnv_vk_backend *b,struct fnv_d3d9_shader_backend *p){
    const uint32_t formats[]={21,20,0x31545844u};unsigned checked=0;
    for(unsigned mode=0;mode<3;mode++){
        uint32_t edge=mode==2?32:8,levels=mode==2?6:4;uintptr_t cube=0;
        uint32_t hr=p->create_cube_texture(p->context,edge,0,0,formats[mode],1,&cube);
        if(mode==2 && hr==FNV_VK_INVALID){puts("cube BC1 skipped: device lacks BC");continue;}
        CHECK(!hr && cube);uint32_t before=submissions(b);
        unsigned char data[8192],expected[8192],out[8192];
        for(unsigned face=0;face<6;face++)for(unsigned level=0;level<levels;level++){
            uint32_t w=edge>>level;if(!w)w=1;
            uint32_t rows=mode==2?(w+3)/4:w,row=mode==2?((w+3)/4)*8:w*(mode==1?3:4),pitch=row+5;
            memset(data,0xa7,sizeof(data));
            for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<row;x++)data[y*pitch+x]=(unsigned char)(face*31+level*17+y*13+x);
            CHECK(!p->upload_cube_texture(p->context,cube,face,level,0,0,w,w,0,pitch,data));
        }
        CHECK(submissions(b)==before); /* All CPU updates coalesce to one version. */
        for(unsigned face=0;face<6;face++)for(unsigned level=0;level<levels;level++){
            uint32_t w=edge>>level;if(!w)w=1;
            uint32_t rows=mode==2?(w+3)/4:w,row=mode==2?((w+3)/4)*8:w*(mode==1?3:4),target=mode==2?row:w*4;
            for(unsigned y=0;y<rows;y++)for(unsigned x=0;x<target;x++){
                unsigned source=mode==2?x:mode==1?(x/4*3+2-x%4):(x/4*4+2-x%4);
                expected[y*target+x]=mode!=2 && x%4==3?(mode==1?255:(unsigned char)(face*31+level*17+y*13+x)):(unsigned char)(face*31+level*17+y*13+source);
            }
            memset(out,0x6d,sizeof(out));
            CHECK(!fnv_vk_backend_read_cube_texture(b,cube,face,level,out,sizeof(out),1000000000ull));
            CHECK(!memcmp(out,expected,rows*target));
            for(unsigned i=rows*target;i<sizeof(out);i++)CHECK(out[i]==0x6d);
            checked++;
        }
        /* Invalid faces/mips and type confusion must not submit or write. */
        before=submissions(b);memset(out,0x6d,sizeof(out));
        CHECK(fnv_vk_backend_read_texture(b,cube,0,out,sizeof(out),1000000000ull)==FNV_VK_INVALID);
        CHECK(fnv_vk_backend_read_cube_texture(b,cube,6,0,out,sizeof(out),1000000000ull)==FNV_VK_INVALID);
        CHECK(fnv_vk_backend_read_cube_texture(b,cube,0,levels,out,sizeof(out),1000000000ull)==FNV_VK_INVALID);
        CHECK(fnv_vk_backend_read_cube_texture(b,cube,0,0,out,1,1000000000ull)==FNV_VK_INVALID);
        CHECK(p->upload_texture(p->context,cube,0,0,0,1,1,0,4,data)==FNV_VK_INVALID);
        CHECK(p->upload_cube_texture(p->context,cube,6,0,0,0,1,1,0,4,data)==FNV_VK_INVALID);
        uintptr_t surface=0;CHECK(p->texture_surface(p->context,cube,0,&surface)==FNV_VK_INVALID);
        CHECK(submissions(b)==before);for(unsigned i=0;i<sizeof(out);i++)CHECK(out[i]==0x6d);
        /* Update one face; other faces and mips survive installation of a new version. */
        memset(data,0x35,sizeof(data));uint32_t row=mode==2?(edge/4)*8:edge*(mode==1?3:4);
        CHECK(!p->upload_cube_texture(p->context,cube,2,0,0,0,edge,edge,0,row,data));
        CHECK(!fnv_vk_backend_read_cube_texture(b,cube,2,0,out,sizeof(out),1000000000ull));
        for(unsigned i=0;i<(mode==2?edge/4*row:edge*edge*4);i++)CHECK(out[i]==(mode==1&&i%4==3?255:0x35));
        CHECK(!fnv_vk_backend_read_cube_texture(b,cube,5,levels-1,out,sizeof(out),1000000000ull));
        CHECK(out[0]==(unsigned char)(5*31+(levels-1)*17+(mode==2?0:2)));
        CHECK(!p->bind_texture(p->context,0,cube));p->release(p->context,cube); /* bound internal pin */
        CHECK(!fnv_vk_backend_read_cube_texture(b,cube,0,0,out,sizeof(out),1000000000ull));
        CHECK(!p->bind_texture(p->context,0,0));
        CHECK(fnv_vk_backend_read_cube_texture(b,cube,0,0,out,sizeof(out),1000000000ull)==FNV_VK_INVALID);
    }
    printf("cube GPU face/mip readbacks=%u; staging, type checks and lifetime OK\n",checked);return 0;
}
