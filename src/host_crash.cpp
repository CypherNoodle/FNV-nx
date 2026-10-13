// Observe the specific low-address fault without changing exception handling.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <rex/ppc/context.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstdlib>
static std::mutex startup_mutex;
static std::condition_variable startup_condition;
static bool startup_object_ready=false;
void FnvObserveBootScope(unsigned function, const char* phase,
                         const PPCContext& ctx, const unsigned char* base) {
  // The allocator serves the entire game. Keep only this constructor's call.
  if(function==0x82219460 && unsigned(ctx.lr)!=0x8260e280) return;
  static std::atomic<unsigned> sequence{0};
  auto read32=[&](unsigned address) {
    unsigned char value[4]{}; SIZE_T received=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base+address,value,4,&received)||received!=4) return 0xffffffffu;
    return (unsigned(value[0])<<24)|(unsigned(value[1])<<16)|(unsigned(value[2])<<8)|value[3];
  };
  const char* mode=std::getenv("FNV_XBOX_STARTUP_BARRIER");
  const bool constructor_barrier=mode && std::strcmp(mode,"1")==0;
  const bool bootstrap_barrier=mode && std::strcmp(mode,"2")==0;
  const bool preloop_barrier=mode && std::strcmp(mode,"3")==0;
  const bool barrier=constructor_barrier||bootstrap_barrier||preloop_barrier;
  bool producer=(constructor_barrier && function==0x8260e250) || (bootstrap_barrier && function==0x82616d38);
  bool checkpoint=preloop_barrier && function==0x82616d38 && std::strcmp(phase,"call_82244E48")==0 && unsigned(ctx.lr)==0x8261740c;
  if(((producer && std::strcmp(phase,"leave")==0)||checkpoint) && read32(0x8308f74c)!=0 && (!checkpoint||read32(0x8308f788)!=0)) {
    { std::lock_guard lock(startup_mutex);startup_object_ready=true; }
    startup_condition.notify_all();
  }
  if(barrier && function==0x82557648 && std::strcmp(phase,"enter")==0) {
    FnvObserveBootScope(function,"barrier_wait",ctx,base);
    std::unique_lock lock(startup_mutex);
    bool ready=startup_condition.wait_for(lock,std::chrono::seconds(10),[]{return startup_object_ready;});
    lock.unlock();
    FnvObserveBootScope(function,ready?"barrier_ready":"barrier_timeout",ctx,base);
    if(!ready) std::abort(); // Fail this experiment; never manufacture success.
  }
  auto index=sequence.fetch_add(1);
  if(index>=500) return;
  char buffer[512];
  int count=std::snprintf(buffer,sizeof(buffer),
    "TRACE n=%u tick=%llu thread=%lu fn=%08x phase=%s r3=%08x r4=%08x lr=%08x global=%08x secondary=%08x heap_flag=%08x heap_owner=%08x startup_flag=%u guest_clock=%u\n",
    index,GetTickCount64(),GetCurrentThreadId(),function,phase,ctx.r3.u32,ctx.r4.u32,unsigned(ctx.lr),
    read32(0x8308f74c),read32(0x8308f788),read32(0x83090f50),read32(0x83091060),
    read32(0x83024ba0)>>24,read32(read32(0x82000718)+16));
  HANDLE file=CreateFileW(L"fnv-host-boot-trace.txt",FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE) {
    DWORD written=0;
    if(count>0&&count<int(sizeof(buffer))) WriteFile(file,buffer,count,&written,nullptr);
    CloseHandle(file);
  }
}
static LONG CALLBACK ObserveHostFault(EXCEPTION_POINTERS* event) {
  auto* record=event->ExceptionRecord;
  if(record->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION||record->NumberParameters<2) return EXCEPTION_CONTINUE_SEARCH;
  if((record->ExceptionInformation[1]&0xffffffffull)>=0x10000) return EXCEPTION_CONTINUE_SEARCH;
#if defined(_M_X64) || defined(__x86_64__)
  HMODULE module=nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(record->ExceptionAddress),&module);
  auto* c=event->ContextRecord; char buffer[1024];
  int count=std::snprintf(buffer,sizeof(buffer),
    "FAULT code=0x%lx ip=0x%llx module_base=0x%llx rva=0x%llx access=0x%llx address=0x%llx thread=%lu\n"
    "REGS rax=0x%llx rcx=0x%llx rdx=0x%llx rbx=0x%llx rsp=0x%llx rbp=0x%llx rsi=0x%llx rdi=0x%llx\n",
    record->ExceptionCode,c->Rip,reinterpret_cast<unsigned long long>(module),
    c->Rip-reinterpret_cast<unsigned long long>(module),record->ExceptionInformation[0],record->ExceptionInformation[1],GetCurrentThreadId(),
    c->Rax,c->Rcx,c->Rdx,c->Rbx,c->Rsp,c->Rbp,c->Rsi,c->Rdi);
  HANDLE file=CreateFileW(L"fnv-host-crash.txt",FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(file!=INVALID_HANDLE_VALUE) {
    DWORD written=0;
    if(count>0&&count<static_cast<int>(sizeof(buffer))) WriteFile(file,buffer,count,&written,nullptr);
    // The confirmed failing function keeps PPCContext* in RSI. Read through
    // the kernel API so an invalid diagnostic pointer cannot raise another AV.
    alignas(64) unsigned char snapshot[sizeof(PPCContext)]; SIZE_T received=0;
    if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<LPCVOID>(c->Rsi),snapshot,sizeof(snapshot),&received)&&received==sizeof(snapshot)) {
      auto field=[&](size_t offset) { unsigned long long value=0;std::memcpy(&value,snapshot+offset,sizeof(value));return value; };
      count=std::snprintf(buffer,sizeof(buffer),"PPC_CANDIDATE r3=0x%llx r1=0x%llx r4=0x%llx r5=0x%llx lr=0x%llx\n",
        field(offsetof(PPCContext,r3)),field(offsetof(PPCContext,r1)),field(offsetof(PPCContext,r4)),field(offsetof(PPCContext,r5)),field(offsetof(PPCContext,lr)));
      if(count>0&&count<static_cast<int>(sizeof(buffer))) WriteFile(file,buffer,count,&written,nullptr);
    }
    CloseHandle(file);
  }
#endif
  return EXCEPTION_CONTINUE_SEARCH;
}
void InstallFnvHostCrashObserver() { AddVectoredExceptionHandler(1,ObserveHostFault); }
