/* Mono IDirectSound3DBuffer state and a bounded stereo software spatializer.
 * The PCM approximation is deliberately separate from Windows ABI validation. */
static float spatial_float(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static struct sound_object *spatial_acquire(uint32_t self){
    struct sound_object *o=acquire(self,1);if(!o)return NULL;
    if(self!=address((unsigned)(o-active->objects))+4 || (o->flags&0x11)!=0x10){
        fnv_host_mutex_unlock(&active->guard);recomp_unimpl("DirectSound spatial identity invalid",self);return NULL;}
    return o;
}
static int spatial_valid(const uint32_t *v,unsigned offset,unsigned count){
    for(unsigned i=0;i<count;i++){unsigned k=offset+i;
        if(k==6 || k==7){if(v[i]>360)return 0;}
        else if(k==11){if((int32_t)v[i]<-10000 || (int32_t)v[i]>0)return 0;}
        else if(k==14){if(v[i]>2)return 0;}
        else {float f=spatial_float(v[i]);if(!isfinite(f) || ((k==12 || k==13) && f<FLT_MIN))return 0;}
    }
    return 1;
}
static void spatial_normalize(uint32_t *v){
    double x[3];for(unsigned i=0;i<3;i++)x[i]=spatial_float(v[i]);double n=sqrt(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]);
    if(n>0)for(unsigned i=0;i<3;i++){float f=(float)(x[i]/n);memcpy(v+i,&f,4);}
}
static void spatial_get(unsigned offset,unsigned count,int all){
    unsigned char *p=range(fnv_cpu_argument_u32(1),all?64:count*4,1);if(!p)return;
    if(all && rd(p)!=64){fnv_cpu_return_u32(INVALID,8);return;}
    struct sound_object *o=spatial_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    if(all)wr(p,64);
    for(unsigned i=0;i<count;i++)wr(p+(all?4:0)+i*4,o->spatial[offset+i]);
    finish(0,8);
}
static void spatial_all_get(void){spatial_get(0,15,1);}
static void spatial_orientation_get(void){spatial_get(8,3,0);}
static void spatial_outside_get(void){spatial_get(11,1,0);}
static void spatial_max_get(void){spatial_get(13,1,0);}
static void spatial_min_get(void){spatial_get(12,1,0);}
static void spatial_mode_get(void){spatial_get(14,1,0);}
static void spatial_position_get(void){spatial_get(0,3,0);}
static void spatial_velocity_get(void){spatial_get(3,3,0);}
static void spatial_angles_get(void){
    unsigned char *a=range(fnv_cpu_argument_u32(1),4,1),*b=range(fnv_cpu_argument_u32(2),4,1);if(!a || !b)return;
    struct sound_object *o=spatial_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    uint32_t inside=o->spatial[6],outside=o->spatial[7];wr(a,inside);wr(b,outside);finish(0,12);
}
static void spatial_set(unsigned offset,unsigned count,int all){
    uint32_t v[15],apply,bytes;
    if(all){unsigned char *p=range(fnv_cpu_argument_u32(1),64,0);if(!p)return;
        if(rd(p)!=64){fnv_cpu_return_u32(INVALID,12);return;}
        for(unsigned i=0;i<15;i++)v[i]=rd(p+4+i*4);
        apply=fnv_cpu_argument_u32(2);bytes=12;
    }else {for(unsigned i=0;i<count;i++)v[i]=fnv_cpu_argument_u32(i+1);apply=fnv_cpu_argument_u32(count+1);bytes=(count+2)*4;}
    if(apply>1 || !spatial_valid(v,offset,count)){fnv_cpu_return_u32(INVALID,bytes);return;}
    if(all)spatial_normalize(v+8);else if(offset==8 && count==3)spatial_normalize(v);
    struct sound_object *o=spatial_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    for(unsigned i=0;i<count;i++){
        if(apply){o->spatial_pending[offset+i]=v[i];o->spatial_dirty|=1u<<(offset+i);}
        else o->spatial[offset+i]=v[i];
    }
    finish(0,bytes);
}
static void spatial_all_set(void){spatial_set(0,15,1);}
static void spatial_angles_set(void){spatial_set(6,2,0);}
static void spatial_orientation_set(void){spatial_set(8,3,0);}
static void spatial_outside_set(void){spatial_set(11,1,0);}
static void spatial_max_set(void){spatial_set(13,1,0);}
static void spatial_min_set(void){spatial_set(12,1,0);}
static void spatial_mode_set(void){spatial_set(14,1,0);}
static void spatial_position_set(void){spatial_set(0,3,0);}
static void spatial_velocity_set(void){spatial_set(3,3,0);}
static double spatial_dot(const double *a,const double *b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void spatial_mix(struct fnv_dsound8 *d,const struct sound_object *o,int32_t gains[2],uint32_t *hz){
    if(o->spatial[14]==2)return;
    float fallback[15]={0,0,0,0,0,0,0,0,1,0,1,0,1,1,1};const float *listener=fallback;
    for(unsigned i=0;i<d->count;i++)if(d->objects[i].refs && d->objects[i].parent==o->parent && (d->objects[i].flags&0x11)==0x11){listener=d->objects[i].listener;break;}
    double relative[3],source_velocity[3],listener_velocity[3],right[3]={1,0,0};
    int head=o->spatial[14]==1;
    for(unsigned i=0;i<3;i++){
        relative[i]=(double)spatial_float(o->spatial[i])-(head?0:listener[i]);
        source_velocity[i]=spatial_float(o->spatial[3+i]);listener_velocity[i]=head?0:listener[3+i];
    }
    if(!head){right[0]=(double)listener[10]*listener[8]-(double)listener[11]*listener[7];right[1]=(double)listener[11]*listener[6]-(double)listener[9]*listener[8];right[2]=(double)listener[9]*listener[7]-(double)listener[10]*listener[6];}
    double distance=sqrt(spatial_dot(relative,relative)),min=spatial_float(o->spatial[12]),max=spatial_float(o->spatial[13]);
    double gain=1,pan=0,doppler=1;
    if(distance>max && (o->flags&0x20000))gain=0;
    else {double clipped=distance>max?max:distance;if(clipped>min)gain=min/(min+(clipped-min)*listener[13]);}
    if(distance>0){
        pan=spatial_dot(relative,right)/distance;if(pan>1)pan=1;if(pan<-1)pan=-1;
        double cone[3];for(unsigned i=0;i<3;i++)cone[i]=spatial_float(o->spatial[8+i]);double norm=sqrt(spatial_dot(cone,cone));
        if(norm>0 && o->spatial[6]<360){
            double cosine=-spatial_dot(cone,relative)/(norm*distance);if(cosine>1)cosine=1;if(cosine<-1)cosine=-1;
            double angle=acos(cosine)*114.59155902616465,inside=o->spatial[6],outside=o->spatial[7];
            double fraction=angle<=inside?0:angle>=outside?1:(angle-inside)/(outside-inside);
            gain*=pow(10.0,((int32_t)o->spatial[11])*fraction/2000.0);
        }
        double scale=listener[12]*listener[14]/distance;
        double toward_listener=spatial_dot(listener_velocity,relative)*scale,away_source=spatial_dot(source_velocity,relative)*scale;
        /* Prevent singular velocities while retaining approach/recede ordering. */
        if(toward_listener>342.9)toward_listener=342.9;
        if(toward_listener<-342.9)toward_listener=-342.9;
        if(away_source>342.9)away_source=342.9;
        if(away_source<-342.9)away_source=-342.9;
        doppler=(343.0+toward_listener)/(343.0+away_source);
    }
    gains[0]=(int32_t)(gains[0]*gain*(pan>0?1-pan:1)+.5);gains[1]=(int32_t)(gains[1]*gain*(pan<0?1+pan:1)+.5);
    double rate=*hz*doppler;if(rate<100)rate=100;if(rate>200000)rate=200000;*hz=(uint32_t)(rate+.5);
}
