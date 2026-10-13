#include "shader_module.h"
#include <cstdio>
#include <cstring>

namespace fnv::deko {
void ShaderModule::Reset() {
  if (code_) dkMemBlockDestroy(code_);
  code_ = nullptr;
  control_.clear();
}
bool ShaderModule::Load(DkDevice device, const char* path) {
  if (!device || !path || code_) return false;
  FILE* file = std::fopen(path, "rb");
  if (!file) return false;
  uint32_t header[6]{};
  if (std::fread(header, sizeof(header), 1, file) != 1 ||
      header[0] != 0x48534B44 || header[1] != sizeof(header) ||
      header[2] < sizeof(header) || header[2] > 1024 * 1024 ||
      !header[3] || header[3] > 16 * 1024 * 1024 ||
      header[4] < sizeof(header) || header[4] >= header[2] || header[5] != 1) {
    std::fclose(file); return false;
  }
  const uint32_t bytes = (header[3] + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1);
  DkMemBlockMaker maker;
  dkMemBlockMakerDefaults(&maker, device, bytes);
  maker.flags |= DkMemBlockFlags_Code;
  code_ = dkMemBlockCreate(&maker);
  if (!code_) { std::fclose(file); return false; }
  control_.resize(header[2]);
  std::rewind(file);
  const bool read = std::fread(control_.data(), control_.size(), 1, file) == 1 &&
                    std::fread(dkMemBlockGetCpuAddr(code_), header[3], 1, file) == 1;
  std::fclose(file);
  if (!read) { Reset(); return false; }
  DkShaderMaker shader_maker;
  dkShaderMakerDefaults(&shader_maker, code_, 0);
  shader_maker.control = control_.data();
  dkShaderInitialize(&shader_, &shader_maker);
  return true;
}
}
