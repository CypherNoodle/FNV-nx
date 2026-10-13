#pragma once
#include <cstddef>
#include <cstdint>

namespace fnv::deko {
struct ConstantField {
  uint32_t source_offset, native_offset, element_bytes, elements;
  uint32_t source_stride, native_stride;
};
// Distinct source/destination allocations. Validate the full plan before writing.
// Copies representation bits, including NaNs and signed zero, without conversion.
bool RepackConstants(const void* source, size_t source_size, void* native,
                     size_t native_size, const ConstantField* fields, size_t count);
}
