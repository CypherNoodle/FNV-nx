#pragma once
#include <cstdint>
namespace fnv {
inline uint64_t TimestampDelta(uint64_t start, uint64_t end, unsigned bits) {
    if (!bits || bits > 64) return 0;
    const uint64_t mask = bits == 64 ? UINT64_MAX : (uint64_t{1} << bits) - 1;
    return (end - start) & mask;
}
}
