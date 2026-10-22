static void light_method(unsigned operation){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0),id=fnv_cpu_argument_u32(1),arg=fnv_cpu_argument_u32(2);
    unsigned bytes=operation<2?104:4;int get=operation==1 || operation==3;
    unsigned char *p=operation==2?NULL:range(c,arg,bytes,get);uint32_t words[26],hr=FNV_D3D9_INVALIDCALL;
    if(p && !get)for(unsigned i=0;i<26;i++)words[i]=read32(p+i*4);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(get && p){struct fnv_light *l=fnv_light_find(&c->lights,id);
        if(l){if(operation==1)for(unsigned i=0;i<26;i++)write32(p+i*4,l->words[i]);else write32(p,l->enabled);hr=0;}
    }else if(!get && (p || operation==2))hr=fnv_light_change(&c->lights,c->recording?&c->recording->lights:NULL,id,operation==0?words:NULL,operation==2?&arg:NULL);
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,12);
}
static void api_set_light(void){light_method(0);}
static void api_get_light(void){light_method(1);}
static void api_light_enable(void){light_method(2);}
static void api_get_light_enable(void){light_method(3);}
