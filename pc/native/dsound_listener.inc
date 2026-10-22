/* IDirectSound3DListener: shares primary identity/refcount, separate vtable.
 * A deferred field survives an immediate update until Commit, like Windows.
 * Only listener state is provided; secondary3D creation still fails explicitly.
 * Ordinary PCM voices do not spatialize and correctly ignore listener state. */
static struct sound_object *listener_acquire(uint32_t self){
    struct sound_object *o=acquire(self,1);if(!o)return NULL;
    if(self!=address((unsigned)(o-active->objects))+4 || (o->flags&0x11)!=0x11){
        fnv_host_mutex_unlock(&active->guard);recomp_unimpl("DirectSound listener identity invalid",self);return NULL;}
    return o;
}
static float listener_float(uint32_t bits){float f;memcpy(&f,&bits,4);return f;}
static void listener_write(unsigned char *p,float f){uint32_t bits;memcpy(&bits,&f,4);wr(p,bits);}
static int listener_orientation(float *v){
    double front[3]={v[0],v[1],v[2]},top[3]={v[3],v[4],v[5]};
    double cross[3]={front[1]*top[2]-front[2]*top[1],front[2]*top[0]-front[0]*top[2],front[0]*top[1]-front[1]*top[0]};
    if(cross[0]==0 && cross[1]==0 && cross[2]==0)return 0;
    double n=0;for(unsigned i=0;i<3;i++)n+=front[i]*front[i];
    if(!(n>0) || !isfinite(n))return 0;
    n=sqrt(n);for(unsigned i=0;i<3;i++)front[i]/=n;
    double dot=0;for(unsigned i=0;i<3;i++)dot+=front[i]*top[i];
    n=0;for(unsigned i=0;i<3;i++){top[i]-=dot*front[i];n+=top[i]*top[i];}
    if(!(n>0) || !isfinite(n))return 0;
    n=sqrt(n);for(unsigned i=0;i<3;i++){v[i]=(float)front[i];v[i+3]=(float)(top[i]/n);}
    return 1;
}
static void listener_get(unsigned offset,unsigned count,int all){
    unsigned char *p=range(fnv_cpu_argument_u32(1),all?64:count*4,1);if(!p)return;
    if(all && rd(p)!=64){fnv_cpu_return_u32(INVALID,8);return;}
    struct sound_object *o=listener_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    if(all)wr(p,64);
    for(unsigned i=0;i<count;i++)listener_write(p+(all?4:0)+i*4,o->listener[offset+i]);
    finish(0,8);
}
static void listener_all_get(void){listener_get(0,15,1);}
static void listener_distance_get(void){listener_get(12,1,0);}
static void listener_doppler_get(void){listener_get(14,1,0);}
static void listener_position_get(void){listener_get(0,3,0);}
static void listener_rolloff_get(void){listener_get(13,1,0);}
static void listener_velocity_get(void){listener_get(3,3,0);}
static void listener_orientation_get(void){
    unsigned char *a=range(fnv_cpu_argument_u32(1),12,1),*b=range(fnv_cpu_argument_u32(2),12,1);if(!a||!b)return;
    struct sound_object *o=listener_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    float front[3],top[3];memcpy(front,o->listener+6,12);memcpy(top,o->listener+9,12);
    for(unsigned i=0;i<3;i++)listener_write(a+i*4,front[i]);
    for(unsigned i=0;i<3;i++)listener_write(b+i*4,top[i]);
    finish(0,12);
}
static int listener_valid(float *v,unsigned offset,unsigned count){
    for(unsigned i=0;i<count;i++){
        if(!isfinite(v[i]))return 0;
        unsigned index=offset+i;
        if(index==12 && v[i]<FLT_MIN)return 0;
        if(index>=13 && (v[i]<0 || v[i]>10))return 0;
    }
    if(offset==6 && count==6)return listener_orientation(v);
    if(offset==0 && count==15)return listener_orientation(v+6);
    return 1;
}
static void listener_set(unsigned offset,unsigned count,int all){
    float values[15];uint32_t apply;unsigned bytes;
    if(all){unsigned char *p=range(fnv_cpu_argument_u32(1),64,0);if(!p)return;
        if(rd(p)!=64){fnv_cpu_return_u32(INVALID,12);return;}
        for(unsigned i=0;i<15;i++)values[i]=listener_float(rd(p+4+i*4));
        apply=fnv_cpu_argument_u32(2);bytes=12;
    }else {
        for(unsigned i=0;i<count;i++)values[i]=listener_float(fnv_cpu_argument_u32(i+1));
        apply=fnv_cpu_argument_u32(count+1);bytes=(count+2)*4;
    }
    if(apply>1 || !listener_valid(values,offset,count)){fnv_cpu_return_u32(INVALID,bytes);return;}
    struct sound_object *o=listener_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    for(unsigned i=0;i<count;i++){
        if(apply){o->listener_pending[offset+i]=values[i];o->listener_dirty|=1u<<(offset+i);}
        else o->listener[offset+i]=values[i];
    }
    finish(0,bytes);
}
static void listener_all_set(void){listener_set(0,15,1);}
static void listener_distance_set(void){listener_set(12,1,0);}
static void listener_doppler_set(void){listener_set(14,1,0);}
static void listener_orientation_set(void){listener_set(6,6,0);}
static void listener_position_set(void){listener_set(0,3,0);}
static void listener_rolloff_set(void){listener_set(13,1,0);}
static void listener_velocity_set(void){listener_set(3,3,0);}
static void listener_commit(void){
    struct sound_object *o=listener_acquire(fnv_cpu_argument_u32(0));if(!o)return;
    for(unsigned i=0;i<15;i++)if(o->listener_dirty&(1u<<i))o->listener[i]=o->listener_pending[i];
    o->listener_dirty=0;finish(0,4);
}
