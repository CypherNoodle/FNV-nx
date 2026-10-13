#include "constant_repack.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

int main() {
  using namespace fnv::deko;
  std::array<uint8_t, 64> source;
  for (size_t i = 0; i < source.size(); ++i) source[i] = uint8_t(i * 73);
  // Preserve the payload bits of NaN and negative zero.
  const uint32_t special[] = {0x7FC12345, 0x80000000};
  std::memcpy(source.data(), special, sizeof(special));
  std::array<uint8_t, 96> native;
  const ConstantField fields[] = {{0, 16, 16, 2, 16, 32}, {48, 80, 8, 1, 8, 8}};
  assert(RepackConstants(source.data(), source.size(), native.data(), native.size(), fields, 2));
  assert(std::memcmp(native.data() + 16, source.data(), 16) == 0);
  assert(std::memcmp(native.data() + 48, source.data() + 16, 16) == 0);
  assert(std::memcmp(native.data() + 80, source.data() + 48, 8) == 0);
  for (size_t i = 0; i < 16; ++i) assert(native[i] == 0);
  const auto unchanged = native;
  assert(!RepackConstants(source.data(), 20, native.data(), native.size(), fields, 2));
  assert(native == unchanged); // No partial write on a rejected plan.
  assert(!RepackConstants(source.data(), source.size(), native.data(), 56, fields, 2));
  assert(native == unchanged);
  assert(!RepackConstants(source.data(), source.size(), source.data(), source.size(), fields, 2));
  ConstantField invalid{0, 0, 4, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  assert(!RepackConstants(source.data(), source.size(), native.data(), native.size(), &invalid, 1));
  invalid = {0, 0, 16, 2, 4, 4};
  assert(!RepackConstants(source.data(), source.size(), native.data(), native.size(), &invalid, 1));
  auto impossible = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max() - 2);
  assert(!RepackConstants(source.data(), source.size(), impossible, native.size(), fields, 2));
  assert(!RepackConstants(nullptr, 0, native.data(), native.size(), fields, 2));
  std::puts("PASS: bits, array strides, bounds, overflow, alias rejection and atomic failure.");
}
