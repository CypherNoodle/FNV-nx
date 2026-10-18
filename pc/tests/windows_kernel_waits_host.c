#define EXTENDED_SYNC_TESTS
#define main event_suite
#include "windows_events_host.c"
#undef main
#define LIST (BASE+0x9000)
#define PREVIOUS (BASE+0x9200)
static uint32_t many(const uint32_t *h,unsigned n,unsigned all,uint32_t ms){
    for(unsigned i=0;i<n;++i)write32(LIST+i*4,h[i]);return call(WAIT_MANY,n,LIST,all,ms);
}
struct multi_work {uint32_t h[3],result;unsigned all,abandon;HANDLE gate,ready;};
static DWORD WINAPI multi_worker(void *context){
    struct multi_work *w=context;struct fnv_windows_thread_config t=thread_config(1);assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    if(w->abandon){assert(call(WAIT_ONE,w->h[2],0,0,0)==0);assert(call(WAIT_ONE,w->h[2],0,0,0)==0);SetEvent(w->ready);assert(WaitForSingleObject(w->gate,5000)==0);}
    else {uint32_t list=BASE+0x9300;for(unsigned i=0;i<3;++i)write32(list+i*4,w->h[i]);w->result=call(WAIT_MANY,3,list,w->all,5000);
        if(w->result==0 || w->result==0x80)assert(call(RELEASE_MUTEX,w->h[2],0,0,0));}
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);return 0;
}
static void join_thread(HANDLE t){assert(WaitForSingleObject(t,5000)==0);DWORD code;assert(GetExitCodeThread(t,&code) && !code);CloseHandle(t);}
static DWORD WINAPI native_abandon(void *context){assert(WaitForSingleObject((HANDLE)context,5000)==0);return 0;}
struct ordered_wait {unsigned index;uint32_t h[2],result;HANDLE done;};
static DWORD WINAPI ordered_worker(void *context){
    struct ordered_wait *w=context;struct fnv_windows_thread_config t=thread_config(w->index+1);
    assert(fnv_windows_thread_attach(process,&t)==FNV_CPU_OK);
    uint32_t at=BASE+0x9400+w->index*16;write32(at,w->h[0]);write32(at+4,w->h[1]);
    w->result=call(WAIT_MANY,2,at,0,5000);
    assert(fnv_windows_thread_detach()==FNV_CPU_OK);assert(SetEvent(w->done));return 0;
}
static void review_regressions(void){
    /* Queue order is deterministic; host thread scheduling order is not. */
    uint32_t ev=call(CREATE_EVENT,0,0,0,0),other=call(CREATE_EVENT,0,0,0,0);assert(ev && other);
    struct ordered_wait w[2];HANDLE threads[2];
    for(unsigned i=0;i<2;++i){w[i]=(struct ordered_wait){i,{ev,other},UINT32_MAX,CreateEventW(NULL,TRUE,FALSE,NULL)};
        assert(w[i].done);threads[i]=CreateThread(NULL,0,ordered_worker,w+i,0,NULL);assert(threads[i]);wait_count(i+1);}
    assert(call(SET_EVENT,ev,0,0,0));assert(WaitForSingleObject(w[0].done,5000)==0);
    assert(WaitForSingleObject(w[1].done,0)==WAIT_TIMEOUT);wait_count(1);
    /* Grant is complete when Set returns. Closing an unselected object must not revoke it. */
    assert(call(SET_EVENT,ev,0,0,0));assert(call(CLOSE_HANDLE,other,0,0,0));
    for(unsigned i=0;i<2;++i){join_thread(threads[i]);assert(w[i].result==0);CloseHandle(w[i].done);}
    assert(call(CLOSE_HANDLE,ev,0,0,0));wait_count(0);
    /* Check ignored low tag bits against the Windows kernel, not a local model. */
    ev=call(CREATE_EVENT,0,1,0,0);HANDLE native=CreateEventW(NULL,TRUE,FALSE,NULL);assert(ev && native && !(ev&3));
    for(unsigned tag=0;tag<4;++tag){HANDLE tagged=(HANDLE)((uintptr_t)native|tag);
        assert(call(SET_EVENT,ev|tag,0,0,0)==(uint32_t)(SetEvent(tagged)!=0));
        assert(call(WAIT_ONE,ev|tag,0,0,0)==WaitForSingleObject(tagged,0));
        assert(call(RESET_EVENT,ev|tag,0,0,0)==(uint32_t)(ResetEvent(tagged)!=0));}
    uint32_t aliases[]={ev,ev|3};HANDLE native_aliases[]={native,(HANDLE)((uintptr_t)native|3)};
    SetLastError(0);uint32_t expected=WaitForMultipleObjects(2,native_aliases,TRUE,0);DWORD error=GetLastError();
    call(SET_ERROR,0,0,0,0);assert(many(aliases,2,1,0)==expected && call(GET_ERROR,0,0,0,0)==error);
    assert(call(CLOSE_HANDLE,ev|3,0,0,0)==(uint32_t)(CloseHandle((HANDLE)((uintptr_t)native|3))!=0));
    assert(call(WAIT_ONE,ev,0,0,0)==WAIT_FAILED);
}
int main(int argc,char **argv){
    if(argc==3)return event_suite(argc,argv);
    assert(event_suite(argc,argv)==0); /* Preserve all event race/strict regressions. */
    arena=VirtualAlloc(NULL,ARENA,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(arena);
    struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,ARENA,&image)==FNV_NATIVE_OK);
    struct fnv_windows_config c={0};c.cpu.memory=arena;c.cpu.guest_base=BASE;c.cpu.memory_bytes=ARENA;c.cpu.code_low=BASE+0x1000;c.cpu.code_high=BASE+image.size;
    c.image_bytes=image.size;c.peb_address=BASE+0x8000;c.process_id=11;c.kernel_handle_capacity=70;
    assert(fnv_windows_create(&c,&process)==FNV_CPU_OK);struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;
    assert(fnv_windows_bind_imports(process,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count)==FNV_NATIVE_OK && count==15);
    struct fnv_windows_thread_config config=thread_config(0);assert(fnv_windows_thread_attach(process,&config)==FNV_CPU_OK);
    review_regressions();
    unsigned seed=0xc2a9,comparisons=0;
    for(unsigned trial=0;trial<200;++trial){
        uint32_t h[]={call(CREATE_EVENT,0,trial&1,trial&2,0),call(CREATE_SEM,0,1,3,0),call(CREATE_MUTEX,0,trial&1,0,0)};
        HANDLE native[]={CreateEventW(NULL,trial&1,trial&2,NULL),CreateSemaphoreW(NULL,1,3,NULL),CreateMutexW(NULL,trial&1,NULL)};
        assert(h[0] && h[1] && h[2] && native[0] && native[1] && native[2]);
        for(unsigned j=0;j<60;++j){seed=seed*1664525u+1013904223u;unsigned op=(seed>>12)%7;
            call(SET_ERROR,0x4567,0,0,0);SetLastError(0x4567);uint32_t got,expected;
            if(op==0){got=call(SET_EVENT,h[0],0,0,0);expected=SetEvent(native[0])!=0;}
            else if(op==1){got=call(RESET_EVENT,h[0],0,0,0);expected=ResetEvent(native[0])!=0;}
            else if(op==2){uint32_t n=(seed>>20)%6;LONG previous=0x12345678;write32(PREVIOUS,0x12345678);
                got=call(RELEASE_SEM,h[1],n,PREVIOUS,0);expected=ReleaseSemaphore(native[1],(LONG)n,&previous)!=0;assert(read32(PREVIOUS)==(uint32_t)previous);}
            else if(op==3){got=call(RELEASE_MUTEX,h[2],0,0,0);expected=ReleaseMutex(native[2])!=0;}
            else if(op==4){unsigned i=(seed>>20)%3;got=call(WAIT_ONE,h[i],0,0,0);expected=WaitForSingleObject(native[i],0);}
            else {got=many(h,3,op==6,0);expected=WaitForMultipleObjects(3,native,op==6,0);}
            DWORD error=GetLastError();assert(got==expected && call(GET_ERROR,0,0,0,0)==error);comparisons++;
        }
        for(unsigned i=0;i<3;++i)assert(call(CLOSE_HANDLE,h[i],0,0,0) && CloseHandle(native[i]));
    }
    for(unsigned i=0;i<5;++i){uint32_t initial[]={0,2,0xffffffff,1,0},max[]={0,1,1,0xffffffff,1};
        SetLastError(0x1234);HANDLE n=CreateSemaphoreW(NULL,(LONG)initial[i],(LONG)max[i],NULL);DWORD error=GetLastError();
        call(SET_ERROR,0x1234,0,0,0);uint32_t h=call(CREATE_SEM_W,0,initial[i],max[i],0);
        assert((h!=0)==(n!=NULL) && call(GET_ERROR,0,0,0,0)==error);if(h){assert(call(CLOSE_HANDLE,h,0,0,0));CloseHandle(n);}}
    uint32_t mutex=call(CREATE_MUTEX_W,0,0,0,0),sem=call(CREATE_SEM_W,0,1,2,0),ev=call(CREATE_W,0,0,0,0);assert(mutex && sem && ev);
    assert(!call(SET_EVENT,sem,0,0,0) && call(GET_ERROR,0,0,0,0)==6);
    assert(!call(RELEASE_SEM,ev,1,0,0) && call(GET_ERROR,0,0,0,0)==6);
    assert(!call(RELEASE_MUTEX,ev,0,0,0) && call(GET_ERROR,0,0,0,0)==6);
    uint32_t list[]={sem,ev,mutex};assert(many(list,3,1,0)==WAIT_TIMEOUT);
    assert(call(WAIT_ONE,sem,0,0,0)==0);assert(call(RELEASE_SEM,sem,1,0,0));
    struct multi_work w={{sem,ev,mutex},0,1,0,NULL,NULL};HANDLE worker=CreateThread(NULL,0,multi_worker,&w,0,NULL);assert(worker);wait_count(1);
    /* Blocked wait-all must leave the semaphore available to another caller. */
    assert(call(WAIT_ONE,sem,0,0,0)==0 && call(RELEASE_SEM,sem,1,0,0));
    assert(call(WAIT_ONE,mutex,0,0,0)==0 && call(RELEASE_MUTEX,mutex,0,0,0));
    assert(call(SET_EVENT,ev,0,0,0) && call(RESET_EVENT,ev,0,0,0));join_thread(worker);assert(w.result==0);
    assert(call(WAIT_ONE,sem,0,0,0)==WAIT_TIMEOUT && call(WAIT_ONE,ev,0,0,0)==WAIT_TIMEOUT);
    assert(call(WAIT_ONE,mutex,0,0,0)==0 && call(RELEASE_MUTEX,mutex,0,0,0));
    for(unsigned round=0;round<100;++round){
        assert(call(WAIT_ONE,mutex,0,0,0)==0 && call(WAIT_ONE,mutex,0,0,0)==0);
        assert(call(RELEASE_SEM,sem,1,0,0) && call(SET_EVENT,ev,0,0,0));
        w.abandon=0;w.result=UINT32_MAX;worker=CreateThread(NULL,0,multi_worker,&w,0,NULL);assert(worker);wait_count(1);
        assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_thread_attach(process,&config)==FNV_CPU_OK);
        join_thread(worker);assert(w.result==WAIT_ABANDONED_0);wait_count(0);
    }
    /* A terminating attached guest thread abandons all recursion levels. */
    w.abandon=1;w.gate=CreateEventW(NULL,FALSE,FALSE,NULL);w.ready=CreateEventW(NULL,FALSE,FALSE,NULL);worker=CreateThread(NULL,0,multi_worker,&w,0,NULL);assert(worker);
    assert(WaitForSingleObject(w.ready,5000)==0);assert(call(WAIT_ONE,mutex,0,0,0)==WAIT_TIMEOUT);
    assert(call(SET_EVENT,ev,0,0,0));SetEvent(w.gate);join_thread(worker);CloseHandle(w.gate);CloseHandle(w.ready);
    HANDLE nm=CreateMutexW(NULL,FALSE,NULL),nt=CreateThread(NULL,0,native_abandon,nm,0,NULL);assert(nm && nt);join_thread(nt);
    assert(call(WAIT_ONE,mutex,0,0,0)==WaitForSingleObject(nm,0));assert(call(RELEASE_MUTEX,mutex,0,0,0) && ReleaseMutex(nm));CloseHandle(nm);
    assert(call(WAIT_ONE,mutex,0,0,0)==0 && call(RELEASE_MUTEX,mutex,0,0,0));
    uint32_t duplicates[]={ev,ev};assert(many(duplicates,2,1,0)==WAIT_FAILED && call(GET_ERROR,0,0,0,0)==87);
    assert(call(WAIT_MANY,0,LIST,0,0)==WAIT_FAILED && call(GET_ERROR,0,0,0,0)==87);
    assert(call(WAIT_MANY,65,LIST,0,0)==WAIT_FAILED && call(GET_ERROR,0,0,0,0)==87);
    uint32_t objects[64];for(unsigned i=0;i<64;++i){objects[i]=call(CREATE_EVENT,0,1,1,0);assert(objects[i]);}
    assert(many(objects,64,1,0)==0);for(unsigned i=0;i<64;++i)assert(call(CLOSE_HANDLE,objects[i],0,0,0));
    for(unsigned i=0;i<3;++i)assert(call(CLOSE_HANDLE,list[i],0,0,0));
    assert(!stats().live_handles && !stats().waiting_threads);assert(fnv_windows_thread_detach()==FNV_CPU_OK && fnv_windows_destroy(process)==FNV_CPU_OK && VirtualFree(arena,0,MEM_RELEASE));
    printf("{\"status\":\"PASS\",\"mixed_windows_comparisons\":%u,\"event_regression_comparisons\":12000,\"event_contention_rounds\":300,\"wait_all_atomic_grant\":true,\"mutex_abandonment\":true,\"pending_abandonment_rounds\":100,\"max_wait_objects\":64,\"oldest_eligible_checked\":true,\"grant_then_close_checked\":true,\"tagged_windows_comparisons\":13,\"adaptive_handle_budgets_checked\":4,\"native_engine_started\":false}\n",comparisons);return 0;
}
