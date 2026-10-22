static struct fnv_d3d9_state_value binding_entry(struct fnv_d3d9_com *c,unsigned sampler,int index){
    uint64_t id=index<0?0:c->resources[index].handle;
    return (struct fnv_d3d9_state_value){.kind=sampler<16?FNV_D3D9_TEXTURE_BINDING:FNV_D3D9_SHADER_BINDING,.sampler=sampler<16?sampler:sampler-16,.values={(uint32_t)id,(uint32_t)(id>>32)}};
}
static uint32_t record_binding(struct fnv_d3d9_com *c,unsigned sampler,int index){
    if(!c->config.backend.resource_pin){c->record_failed=1;return 0x8876086au;}
    struct state_block *b=c->recording;struct fnv_d3d9_state_value entry=binding_entry(c,sampler,index);
    uint32_t hr=c->config.backend.state_batch(c->config.backend.context,&entry,1,2);if(hr)return hr;
    int old=b->binding_mask&(1u<<sampler)?b->bindings[sampler]:-1;
    if(index!=old){if(index>=0){c->config.backend.resource_pin(c->config.backend.context,c->resources[index].handle,1);c->resources[index].pins++;c->resources[index].state_pins++;}
        if(old>=0){c->config.backend.resource_pin(c->config.backend.context,c->resources[old].handle,0);c->resources[old].pins--;c->resources[old].state_pins--;retire(c,old);}}
    b->bindings[sampler]=index;b->binding_mask|=1u<<sampler;return 0;
}
static uint32_t record_numeric(struct fnv_d3d9_com *c,enum fnv_d3d9_state_kind kind,uint32_t sampler,uint32_t name,const uint32_t values[6]){
    struct fnv_d3d9_state_value entry={.kind=kind,.sampler=sampler,.name=name};memcpy(entry.values,values,24);
    uint32_t hr=c->config.backend.state_batch(c->config.backend.context,&entry,1,2);if(hr)return hr;
    uint32_t i=0;while(i<c->recording->count){struct fnv_d3d9_state_value *e=c->recording->entries+i;if(e->kind==kind && e->sampler==sampler && e->name==name)break;i++;}
    if(i==256)return FNV_D3D9_OUTOFMEMORY;
    c->recording->entries[i]=entry;if(i==c->recording->count)c->recording->count++;return 0;
}
static void api_begin_stateblock(void){
    struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);(void)object(c,fnv_cpu_argument_u32(0),1);uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(!c->recording){hr=0x8876086au;if(c->config.backend.state_batch){hr=c->config.backend.state_batch(c->config.backend.context,NULL,0,2);
        if(!hr){c->recording=calloc(1,sizeof(*c->recording));hr=c->recording?0:FNV_D3D9_OUTOFMEMORY;c->record_failed=0;}}}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,4);
}
static void api_end_stateblock(void){
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,fnv_cpu_argument_u32(1),4,1);fnv_host_mutex_lock(&c->guard);(void)object(c,fnv_cpu_argument_u32(0),1);
    uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(!c->recording || !out)goto done;
    write32(out,0);
    if(c->record_failed){struct state_block *discard=c->recording;c->recording=NULL;free_stateblock(c,discard);c->record_failed=0;hr=0x8876086au;goto done;}
    uint32_t index=0;while(index<c->used && c->resources[index].handle)index++;
    hr=FNV_D3D9_OUTOFMEMORY;if(index==c->config.resource_capacity || c->device_references==UINT32_MAX)goto done;
    c->resources[index]=(struct resource){.handle=(uintptr_t)c->recording,.kind=FNV_D3D9_STATEBLOCK,.references=1,.snapshot=c->recording};
    c->recording=NULL;if(index==c->used)c->used++;
    write32(c->objects+OBJECT_OFFSET+index*4,c->config.objects_address+BLOCK_VTABLE);write32(out,resource_address(c,index));
    c->device_references++;c->config.backend.device_addref(c->config.backend.context);hr=0;
done:fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,8);
}
static void stateblock_operation(uint32_t operation){
    struct fnv_d3d9_com *c=module();fnv_host_mutex_lock(&c->guard);int index=resource_kind(c,fnv_cpu_argument_u32(0),FNV_D3D9_STATEBLOCK);uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(index>=0 && !c->recording){struct state_block *b=c->resources[index].snapshot;
        /* All mask validation precedes the numeric transaction. Under the
         * module lock, the following constant commit cannot fail validation. */
        hr=fnv_d3d9_constants_snapshot(c->constants,&b->constants,2);
        struct fnv_d3d9_state_value entries[274];uint32_t count=b->count;memcpy(entries,b->entries,count*sizeof(*entries));
        if(operation==0)for(unsigned i=0;i<18;++i)if(b->binding_mask&(1u<<i))entries[count++]=binding_entry(c,i,b->bindings[i]);
        if(!hr)hr=c->config.backend.state_batch(c->config.backend.context,entries,count,operation);
        if(!hr){
            if(operation==1){
                memcpy(b->entries,entries,b->count*sizeof(*entries));
                for(unsigned i=0;i<18;++i)if((b->binding_mask&(1u<<i)) && *block_slot(c,i)!=b->bindings[i] && *block_slot(c,i)>=0){int index=*block_slot(c,i);c->config.backend.resource_pin(c->config.backend.context,c->resources[index].handle,1);c->resources[index].pins++;c->resources[index].state_pins++;}
                for(unsigned i=0;i<18;++i)if((b->binding_mask&(1u<<i)) && *block_slot(c,i)!=b->bindings[i]){int old=b->bindings[i];b->bindings[i]=*block_slot(c,i);if(old>=0){c->config.backend.resource_pin(c->config.backend.context,c->resources[old].handle,0);c->resources[old].pins--;c->resources[old].state_pins--;retire(c,old);}}
            }else for(unsigned i=0;i<18;++i)if(b->binding_mask&(1u<<i))change_binding(c,block_slot(c,i),b->bindings[i]);
        }
        if(!hr)hr=fnv_d3d9_constants_snapshot(c->constants,&b->constants,operation);
        if(!hr)for(unsigned i=0;i<266;i++)if(b->transform_mask[i/32]&(1u<<(i%32))){
            if(operation==1)memcpy(b->transforms.matrices[i],c->transforms.matrices[i],64);
            else memcpy(c->transforms.matrices[i],b->transforms.matrices[i],64);
        }
    }
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,4);
}
static void api_capture_stateblock(void){stateblock_operation(1);}
static void api_apply_stateblock(void){stateblock_operation(0);}
static void api_create_stateblock(void){
    struct fnv_d3d9_com *c=module();unsigned char *out=range(c,fnv_cpu_argument_u32(2),4,1);uint32_t type=fnv_cpu_argument_u32(1);
    fnv_host_mutex_lock(&c->guard);(void)object(c,fnv_cpu_argument_u32(0),1);uint32_t hr=FNV_D3D9_INVALIDCALL;
    if(out && type>=1 && type<=3){write32(out,0);hr=0x8876086au;}
    fnv_host_mutex_unlock(&c->guard);fnv_cpu_return_u32(hr,12);
}
