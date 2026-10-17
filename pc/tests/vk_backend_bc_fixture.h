/* Raw block preservation and GPU sampling are separate checks. */
static unsigned bc_pixel_checks,bc_transfer_checks;
static const uint32_t bc_formats[3]={0x31545844,0x33545844,0x35545844};
static void exercise_bc_transfers(void){
    for(unsigned format=0;format<3;++format){
        unsigned unit=format?16:8,pitch;uint32_t texture=make_texture(12,8,0,0,bc_formats[format],1);uintptr_t id=texture_id;
        unsigned char base[96];assert(call(T_LEVELS,1,&texture)==4);
        for(unsigned level=0;level<4;++level){unsigned w=12>>level,h=8>>level;if(!w)w=1;if(!h)h=1;
            unsigned row=((w+3)/4)*unit,rows=(h+3)/4,bytes=row*rows;unsigned char expected[96];
            unsigned char *p=lock_texture(texture,level,0,0,&pitch);assert(pitch==row);
            for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<row;++x)p[y*pitch+x]=expected[y*row+x]=(unsigned char)(format*31+level*17+y*row+x);
            assert(!unlock_texture(texture,level));check_texture(id,level,expected,bytes);bc_transfer_checks++;
            if(!level)memcpy(base,expected,bytes);
        }
        const uint32_t rectangle[4]={8,4,12,8};memcpy(ptr(DATA),rectangle,16);unsigned char *p=lock_texture(texture,0,DATA,0,&pitch);memset(p,0xab,unit);
        assert(!unlock_texture(texture,0));uint64_t allocated=stats().allocated_bytes;fail_Image=1;unsigned char failed[96];memset(failed,0xcd,sizeof(failed));assert(fnv_vk_backend_read_texture(gpu,id,0,failed,6*unit,10000000000ull)==FNV_VK_OOM);assert(stats().allocated_bytes==allocated);for(unsigned j=0;j<96;++j)assert(failed[j]==0xcd);
        memset(base+5*unit,0xab,unit);check_texture(id,0,base,6*unit);bc_transfer_checks+=2;
        unsigned char small[96];memset(small,0xcd,sizeof(small));assert(fnv_vk_backend_read_texture(gpu,id,0,small,6*unit-1,1)==FNV_VK_INVALID);for(unsigned i=0;i<96;++i)assert(small[i]==0xcd);
        const unsigned char block[16]={0};assert(provider.upload_texture(gpu,id,0,1,0,4,4,0,unit,block)==FNV_VK_INVALID);
        assert(provider.upload_texture(gpu,id,0,0,0,4,4,0,unit-1,block)==FNV_VK_INVALID);
        assert(provider.upload_texture(gpu,id,0,0,0,3,4,0,unit,block)==FNV_VK_INVALID);bc_transfer_checks+=4;
        assert(!release(texture));
    }
}
static void exercise_bc_draws(uint32_t vb,uint32_t ib,uint32_t restore){
    exercise_bc_transfers();set_render(174,0);set_render(7,0);set_render(27,0);set_render(22,1);set_render(168,15);
    draw_geometry(vb,ib,0);draw_constants(0,.5f);shader_rgba();
    uint32_t sampler[]={device_at,0,1,3};assert(!call(D_SAMPLER_SET,4,sampler));sampler[2]=2;assert(!call(D_SAMPLER_SET,4,sampler));
    for(unsigned format=0;format<3;++format){
        uint32_t texture=make_texture(8,8,1,0,bc_formats[format],1);unsigned unit=format?16:8,pitch;
        unsigned char *p=lock_texture(texture,0,0,0,&pitch);unsigned char expected[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,255,255}};
        const unsigned colors[4]={0xf800,0x07e0,0x001f,0xffff};
        for(unsigned block=0;block<4;++block){unsigned char *b=p+(block/2)*pitch+(block%2)*unit;memset(b,0,unit);unsigned char *color=b+(format?8:0);color[0]=(unsigned char)colors[block];color[1]=(unsigned char)(colors[block]>>8);
            if(!format && block==3){color[0]=color[1]=0;color[2]=color[3]=255;memset(color+4,255,4);memset(expected[3],0,4);}
            if(format==1){memset(b,(int)(block*0x55),8);expected[block][3]=(unsigned char)(block*85);}
            if(format==2){b[0]=255;b[1]=0;const unsigned indices[4]={0,1,2,7};uint64_t bits=0;for(unsigned i=0;i<16;++i)bits|=(uint64_t)indices[block]<<(i*3);for(unsigned i=0;i<6;++i)b[i+2]=(unsigned char)(bits>>(i*8));const unsigned alpha[4]={255,0,219,36};expected[block][3]=(unsigned char)alpha[block];}
        }
        assert(!unlock_texture(texture,0));texture_ready(texture);uint32_t bind[]={device_at,0,texture};assert(!call(T_BIND,3,bind));clear_color(0xff112233);
        /* Linear capability must be checked for BC, not the RGBA8 fallback. */
        hide_bc_linear=1;sampler[2]=5;sampler[3]=2;assert(!call(D_SAMPLER_SET,4,sampler));assert(!call(BEGIN,1,&device_at));
        uint32_t rejected[]={device_at,4,UINT32_MAX,1,4,2,2};assert(call(D_INDEXED,7,rejected)==FNV_VK_INVALID);assert(!call(END,1,&device_at));hide_bc_linear=0;
        sampler[3]=1;assert(!call(D_SAMPLER_SET,4,sampler));
        assert(!call(BEGIN,1,&device_at));uint32_t args[]={device_at,4,UINT32_MAX,1,4,2,2};assert(!call(D_INDEXED,7,args));assert(!call(END,1,&device_at));
        unsigned char actual[4096];assert(!fnv_vk_backend_read_color(gpu,native_color(),actual,sizeof(actual),10000000000ull));
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)for(unsigned c=0;c<4;++c){unsigned wanted=expected[(y>=16)*2+(x>=16)][c==0?2:c==2?0:c],got=actual[(y*32+x)*4+c];
            if((got>wanted?got-wanted:wanted-got)>1){fprintf(stderr,"BC sample format=%u pixel=%u,%u channel=%u actual=%u expected=%u\n",format,x,y,c,got,wanted);exit(1);}}
        bc_pixel_checks+=1024;bind[2]=restore;assert(!call(T_BIND,3,bind));assert(!release(texture));
    }
    sampler[2]=1;sampler[3]=2;assert(!call(D_SAMPLER_SET,4,sampler));draw_mirrored=1;draw_geometry(vb,ib,1);
}
