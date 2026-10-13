#include "constant_repack.h"
#include <cstring>
#include <limits>

namespace fnv::deko {
bool RepackConstants(const void* source, size_t source_size, void* native,
                     size_t native_size, const ConstantField* fields, size_t count) {
  if (!source || !native || (!fields && count)) return false;
  const auto s = reinterpret_cast<uintptr_t>(source);
  const auto d = reinterpret_cast<uintptr_t>(native);
  constexpr auto max = std::numeric_limits<uintptr_t>::max();
  if (source_size > max - s || native_size > max - d) return false;
  if (s < d + native_size && d < s + source_size) return false;
  auto fits = [](size_t offset, size_t stride, const ConstantField& f, size_t size) {
    if (!f.elements || !f.element_bytes || offset > size || f.element_bytes > size - offset)
      return false;
    if (f.elements == 1) return true;
    return stride >= f.element_bytes &&
           size_t(f.elements - 1) <= (size - offset - f.element_bytes) / stride;
  };
  for (size_t i = 0; i < count; ++i) {
    if (!fits(fields[i].source_offset, fields[i].source_stride, fields[i], source_size) ||
        !fits(fields[i].native_offset, fields[i].native_stride, fields[i], native_size))
      return false;
  }
  std::memset(native, 0, native_size);
  for (size_t i = 0; i < count; ++i) {
    const auto& f = fields[i];
    for (uint32_t j = 0; j < f.elements; ++j)
      std::memcpy(static_cast<uint8_t*>(native) + f.native_offset + size_t(j) * f.native_stride,
                  static_cast<const uint8_t*>(source) + f.source_offset + size_t(j) * f.source_stride,
                  f.element_bytes);
  }
  return true;
}
}
