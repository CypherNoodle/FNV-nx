/* D3DMATERIAL9 is 17 binary32 words, serialized explicitly for x86 guests.
 * Values are stored verbatim, including power outside the usual lighting range.
 * Windows HAL observation: initial material zero; recording does not change live. */
static void material_method(int get){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);
    unsigned char *p=range(c,fnv_cpu_argument_u32(1),68,get);
    uint32_t words[17],hr=FNV_D3D9_INVALIDCALL;
    if(p && !get)for(unsigned i=0;i<17;i++)words[i]=read32(p+i*4);
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(p){
        if(get)for(unsigned i=0;i<17;i++)write32(p+i*4,c->material[i]);
        else if(c->recording){memcpy(c->recording->material,words,68);c->recording->material_set=1;}
        else memcpy(c->material,words,68);
        hr=0;
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,8);
}
static void api_set_material(void){material_method(0);}
static void api_get_material(void){material_method(1);}
