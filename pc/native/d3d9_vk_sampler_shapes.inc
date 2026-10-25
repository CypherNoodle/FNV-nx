/* Shape metadata only, for already offline-validated linked SPIR-V.
 * No instruction translation and no guest-provided modules. Reject arrays,
 * separate samplers, multisample/depth/array images and non-2D/cube profiles. */
static uint32_t sampler_shapes(const void *code,uint32_t bytes,uint32_t mask,uint32_t *cube_mask){
    struct shape_id {uint32_t kind,ref,storage,dim,flags,set,binding,decor;};
    if(!code || bytes<20 || bytes%4 || !cube_mask)return FNV_VK_INVALID;
    const unsigned char *p=code;uint32_t bound=word(p+12),seen=0,cubes=0,hr=FNV_VK_INVALID;
    if(!bound || bound>65536)return hr;
    struct shape_id *ids=calloc(bound,sizeof(*ids));if(!ids)return FNV_VK_OOM;
    for(uint32_t at=20;at<bytes;){uint32_t op=word(p+at),n=op>>16;op&=65535;
        if(!n || n>(bytes-at)/4)goto done;
        uint32_t id=0;struct shape_id *s=NULL;
        if(op==25 || op==27 || op==32){if(n<2)goto done;id=word(p+at+4);}
        else if(op==59){if(n<4)goto done;id=word(p+at+8);}
        if(id){if(id>=bound || ids[id].kind)goto done;s=ids+id;s->kind=op;}
        if(op==25){if(n!=9 || !s)goto done;s->dim=word(p+at+12);s->flags=word(p+at+16)|word(p+at+20)|word(p+at+24);if(word(p+at+28)!=1)s->flags|=1;}
        else if(op==27){if(n!=3 || !s)goto done;s->ref=word(p+at+8);if(s->ref>=bound)goto done;}
        else if(op==32){if(n!=4 || !s)goto done;s->storage=word(p+at+8);s->ref=word(p+at+12);if(s->ref>=bound)goto done;}
        else if(op==59){if(!s)goto done;s->ref=word(p+at+4);s->storage=word(p+at+12);if(s->ref>=bound)goto done;}
        else if(op==71){if(n<3)goto done;id=word(p+at+4);if(id>=bound)goto done;uint32_t d=word(p+at+8);
            if(d==33 || d==34){if(n!=4)goto done;s=ids+id;uint32_t v=word(p+at+12),bit=d==33?1:2;
                if((s->decor&bit) && (d==33?s->binding:s->set)!=v)goto done;
                if(d==33)s->binding=v;else s->set=v;s->decor|=bit;}}
        at+=n*4;
    }
    for(uint32_t i=1;i<bound;i++)if(ids[i].kind==59 && !ids[i].storage){
        struct shape_id *v=ids+i,*ptr=ids+v->ref;
        if(ptr->kind!=32 || ptr->storage || v->decor!=3 || v->set!=2 || v->binding>=16 || (seen&(1u<<v->binding)))goto done;
        struct shape_id *sample=ids+ptr->ref;if(sample->kind!=27)goto done;
        struct shape_id *image=ids+sample->ref;if(image->kind!=25 || image->flags || (image->dim!=1 && image->dim!=3))goto done;
        seen|=1u<<v->binding;if(image->dim==3)cubes|=1u<<v->binding;
    }
    if(seen!=mask)goto done;
    *cube_mask=cubes;hr=0;
 done:free(ids);return hr;
}
