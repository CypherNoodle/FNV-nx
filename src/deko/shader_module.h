#pragma once
#include <deko3d.h>
#include <vector>

namespace fnv::deko {
// Prototype module ownership only. The caller must finish GPU work before reset/destruction.
class ShaderModule {
 public:
  ShaderModule() = default;
  ShaderModule(const ShaderModule&) = delete;
  ShaderModule& operator=(const ShaderModule&) = delete;
  ~ShaderModule() { Reset(); }
  bool Load(DkDevice device, const char* file);
  void Reset();
  const DkShader* Get() const { return code_ ? &shader_ : nullptr; }
 private:
  DkMemBlock code_ = nullptr;
  DkShader shader_{};
  std::vector<uint8_t> control_;
};
}
