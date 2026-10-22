/* Matrix storage under COM guard; fixed-function draw consumption is pending. */
static void transform_method(unsigned operation){
    struct fnv_d3d9_com *c=module();uint32_t self=fnv_cpu_argument_u32(0);
    uint32_t state=fnv_cpu_argument_u32(1),at=fnv_cpu_argument_u32(2);
    unsigned char *p=range(c,at,64,operation==1);float matrix[16];
    int index=fnv_transform_index(state);uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(p && operation!=1)for(unsigned i=0;i<16;i++){uint32_t bits=read32(p+i*4);memcpy(matrix+i,&bits,4);}
    fnv_host_mutex_lock(&c->guard);(void)object(c,self,1);
    if(p && index>=0){
        if(operation==1){
            hr=fnv_transform_get(&c->transforms,state,matrix);
            if(!hr)for(unsigned i=0;i<16;i++){uint32_t bits;memcpy(&bits,matrix+i,4);write32(p+i*4,bits);}
        }else if(operation==0 && c->recording){
            hr=fnv_transform_set(&c->recording->transforms,state,matrix);
            if(!hr)c->recording->transform_mask[(unsigned)index/32]|=1u<<((unsigned)index%32);
        }else if(operation==0)hr=fnv_transform_set(&c->transforms,state,matrix);
        /* Windows MultiplyTransform changes live state even while recording.
         * It is not equivalent to GetTransform+recorded SetTransform. */
        else hr=fnv_transform_multiply(&c->transforms,state,matrix);
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,12);
}
static void api_set_transform(void){transform_method(0);}
static void api_get_transform(void){transform_method(1);}
static void api_multiply_transform(void){transform_method(2);}
