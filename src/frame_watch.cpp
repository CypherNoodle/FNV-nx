// Observe the SDK guest output, never the user's desktop or other windows.
#define NOMINMAX
#include <windows.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdint>

std::jthread StartFnvFrameWatch(rex::Runtime* runtime) {
  return std::jthread([runtime](std::stop_token stop) {
    auto* base=runtime->virtual_membase();
    auto read32=[&](uint32_t address) {
      unsigned char bytes[4]{};SIZE_T count=0;
      if(!ReadProcessMemory(GetCurrentProcess(),base+address,bytes,4,&count)||count!=4) return 0xffffffffu;
      return (uint32_t(bytes[0])<<24)|(uint32_t(bytes[1])<<16)|(uint32_t(bytes[2])<<8)|bytes[3];
    };
    for(unsigned sample=0;sample<120&&!stop.stop_requested();++sample) {
      for(unsigned tenth=0;tenth<10&&!stop.stop_requested();++tenth)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if(stop.stop_requested()) break;
      auto* graphics=runtime->graphics_system();
      auto* presenter=graphics?graphics->presenter():nullptr;
      rex::ui::RawImage frame;
      bool captured=false;
      bool attempted=presenter && (sample==4||sample==9||sample==19||sample==29||sample==44||sample==59||sample==74||sample==89);
      if(attempted)
        captured=presenter->CaptureGuestOutput(frame);
      unsigned long long rgb_sum=0;
      bool valid=captured&&frame.width&&frame.height&&frame.stride>=size_t(frame.width)*4&&
        frame.data.size()>=size_t(frame.height-1)*frame.stride+size_t(frame.width)*4;
      if(valid) {
        char filename[128];std::snprintf(filename,sizeof(filename),"fnv-host-guest-%lu-%02u.ppm",GetCurrentProcessId(),sample+1);
        FILE* file=std::fopen(filename,"wb");
        if(file) {
          std::fprintf(file,"P6\n%u %u\n255\n",frame.width,frame.height);
          for(unsigned y=0;y<frame.height;++y) for(unsigned x=0;x<frame.width;++x) {
            const auto* pixel=frame.data.data()+size_t(y)*frame.stride+size_t(x)*4;
            rgb_sum+=pixel[0]+pixel[1]+pixel[2];std::fwrite(pixel,1,3,file);
          }
          std::fclose(file);
        }
      }
      uint32_t state=read32(0x83077ddc);
      uint32_t flags=state&&state!=0xffffffffu?read32(state):0xffffffffu;
      FILE* log=std::fopen("fnv-host-frame-watch.txt","ab");
      if(log) {
        std::fprintf(log,"WATCH pid=%lu sample=%u tick=%llu presentation=%u presenter=%u attempted=%u capture=%u valid=%u width=%u height=%u rgb_sum=%llu state=%08x flags=%08x\n",
          GetCurrentProcessId(),sample+1,GetTickCount64(),graphics&&graphics->has_presentation(),presenter!=nullptr,
          attempted,captured,valid,frame.width,frame.height,rgb_sum,state,flags);
        std::fclose(log);
      }
    }
  });
}
