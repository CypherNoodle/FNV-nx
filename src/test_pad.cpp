// Opt-in guest input experiment. Does not send input to Windows or other apps.
#include <rex/runtime.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <chrono>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
namespace {
using rex::X_RESULT; // The SDK X_ERROR_* macros cast to this unqualified type.
class TestPad final : public rex::input::InputDriver {
 public:
  TestPad():InputDriver(nullptr,0),start_(std::chrono::steady_clock::now()) {}
  rex::X_STATUS Setup() override { return 0; }
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& devices) override {
    devices.push_back({id_,0,"FNV host test pad","fnv-local-test-pad",true});
  }
  rex::X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* state) override {
    if(id!=id_) return X_ERROR_DEVICE_NOT_CONNECTED;
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_).count();
    uint16_t buttons=ms>=8000&&ms<9000?rex::input::X_INPUT_GAMEPAD_START:
                     ms>=14000&&ms<15000?rex::input::X_INPUT_GAMEPAD_A:0;
    unsigned old=last_.exchange(buttons);
    if(old!=buttons) ++packet_;
    *state={};state->packet_number=packet_.load();state->gamepad.buttons=buttons;
    if(old!=buttons||queries_.fetch_add(1)==0) {
      FILE* log=std::fopen("fnv-host-test-pad.txt","ab");
      if(log) {std::fprintf(log,"PAD elapsed_ms=%lld buttons=%04x packet=%u\n",static_cast<long long>(ms),buttons,packet_.load());std::fclose(log);}
    }
    return X_ERROR_SUCCESS;
  }
  rex::X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t,rex::input::X_INPUT_CAPABILITIES* caps) override {
    if(id!=id_) return X_ERROR_DEVICE_NOT_CONNECTED;
    *caps={};caps->type=rex::input::XINPUT_DEVTYPE_GAMEPAD;caps->sub_type=1;
    caps->gamepad.buttons=0xffff;return X_ERROR_SUCCESS;
  }
  rex::X_RESULT SetDeviceVibration(rex::input::DeviceId id,rex::input::X_INPUT_VIBRATION*) override {
    return id==id_?X_ERROR_SUCCESS:X_ERROR_DEVICE_NOT_CONNECTED;
  }
  rex::X_RESULT GetDeviceKeystroke(rex::input::DeviceId,uint32_t,rex::input::X_INPUT_KEYSTROKE*) override {return X_ERROR_EMPTY;}
 private:
  const rex::input::DeviceId id_=static_cast<rex::input::DeviceId>(0x464e56ffffff0001ull);
  std::chrono::steady_clock::time_point start_;
  std::atomic<unsigned> last_{0xffff},packet_{0},queries_{0};
};
}
void InstallFnvTestPad(rex::Runtime* runtime) {
  const char* mode=std::getenv("FNV_XBOX_TEST_PAD");
  if(!mode||std::strcmp(mode,"1")!=0) return;
  auto* input=dynamic_cast<rex::input::InputSystem*>(runtime->input_system());
  if(!input) {REXLOG_ERROR("FNV test pad: incompatible input backend");return;}
  input->AddDriver(std::make_unique<TestPad>());
  input->SetActiveCallback([]{return true;});
  REXLOG_INFO("FNV synthetic test pad enabled: START at 8 s, A at 14 s");
}
