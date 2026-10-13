#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

// A guest-authorized store may use backing only inside the checked fault page.
// Reject loads, unknown operations, page crossings, overflow and unrelated FARs.
inline bool FnvShadowStoreSpan(bool store, uint64_t address, size_t bytes,
                               uint64_t far, uint64_t authorized) {
    if (!store || !bytes || bytes > 32 || far != authorized ||
        address > std::numeric_limits<uint64_t>::max() - (bytes - 1)) return false;
    const uint64_t last = address + bytes - 1;
    return address <= far && far <= last &&
           (address >> 12) == (far >> 12) && (last >> 12) == (far >> 12);
}
