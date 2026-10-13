// Exercise the actual SDK-injected loop with deterministic kernel-call doubles.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include "../tools/horizon_shadow_write.h"
using u64 = uint64_t;
using Result = unsigned;
#define R_FAILED(rc) ((rc) != 0)
enum RexGmAccess { REX_GM_NONE, REX_GM_READ, REX_GM_WRITE };
constexpr size_t kPageSize = 4096;
struct View { uint8_t* base; size_t offset, length; };
struct Chunk { size_t offset, length; void* shadow; };
struct State {
    uint8_t* base;
    std::map<size_t, RexGmAccess> protection;
    std::set<std::pair<size_t, size_t>> view_mapped;
    std::vector<Chunk> chunks;
    size_t mapped;
    uint32_t last_result = 0;
    uint64_t fnv_protect_runs = 0, fnv_protect_pages = 0, fnv_protect_failures = 0;
};
uint8_t window[16 * kPageSize], shadow[16 * kPageSize];
View view{window, 0, sizeof(window)};
RexGmAccess AccessOf(State& s, size_t p) {
    auto it = s.protection.find(p);
    return it == s.protection.end() ? REX_GM_WRITE : it->second;
}
const View* ViewFor(State&, u64) { return &view; }
bool ViewIndexFor(State&, u64, size_t* vi) { *vi = 0; return true; }
Chunk* ChunkAt(State& s, size_t off) {
    for (auto& c : s.chunks) if (off >= c.offset && off < c.offset + c.length) return &c;
    return nullptr;
}
bool ProtectWithPermissions(uint8_t*, bool, size_t) { return false; }
int Proc() { return 1; }
std::vector<std::pair<size_t, size_t>> calls;
Result next_result = 0;
Result svcMapProcessMemory(uint8_t* addr, int, u64, size_t length) {
    calls.emplace_back(addr - window, length); return next_result;
}
Result svcUnmapProcessMemory(uint8_t* addr, int p, u64 src, size_t length) {
    return svcMapProcessMemory(addr, p, src, length);
}
bool protect(State& s, size_t begin, size_t end, RexGmAccess access) {
    const u64 wbase = reinterpret_cast<u64>(s.base);
    const bool must_be = access == REX_GM_WRITE;
#include "../tools/horizon_protect_runs.inc"
    return true;
}
State state() {
    calls.clear(); next_result = 0;
    State s{}; s.base = window; s.mapped = sizeof(window);
    s.chunks = {{0, 8*kPageSize, shadow}, {8*kPageSize, 8*kPageSize, shadow+8*kPageSize}};
    s.view_mapped = {{0,0}, {0,8*kPageSize}};
    return s;
}
int main() {
    assert(FnvShadowStoreSpan(true, 0xB9ECE004, 4, 0xB9ECE004, 0xB9ECE004));
    assert(!FnvShadowStoreSpan(false, 0x1000, 4, 0x1000, 0x1000));
    assert(!FnvShadowStoreSpan(true, 0x1FFE, 4, 0x2000, 0x2000));
    assert(!FnvShadowStoreSpan(true, UINT64_MAX, 2, UINT64_MAX, UINT64_MAX));
    assert(!FnvShadowStoreSpan(true, 0x1000, 4, 0x1004, 0x1004));
    assert(!FnvShadowStoreSpan(true, 0x1000, 4, 0x1000, 0x1001));
    auto s = state();
    assert(protect(s, kPageSize, 12*kPageSize, REX_GM_READ));
    assert(calls.size() == 2 && calls[0] == std::make_pair(kPageSize,7*kPageSize));
    assert(calls[1] == std::make_pair(8*kPageSize,4*kPageSize));
    assert(s.protection.size() == 11 && s.mapped == 5*kPageSize);
    calls.clear();
    assert(protect(s, kPageSize, 12*kPageSize, REX_GM_WRITE));
    assert(calls.size() == 2 && s.protection.empty() && s.mapped == sizeof(window));
    s = state(); s.protection[3*kPageSize] = REX_GM_NONE; s.mapped -= kPageSize;
    assert(protect(s, 2*kPageSize, 5*kPageSize, REX_GM_READ));
    assert(calls.size() == 2 && calls[0].second == kPageSize && calls[1].first == 4*kPageSize);
    assert(s.protection.size() == 3); // no changes outside requested pages
    s = state(); next_result = 0xCE01;
    assert(!protect(s, 2*kPageSize, 7*kPageSize, REX_GM_READ));
    assert(s.protection.empty() && s.mapped == sizeof(window));
    assert(s.last_result == 0xCE01 && s.fnv_protect_failures == 1);
    s = state(); s.view_mapped.clear();
    assert(protect(s, 0, 3*kPageSize, REX_GM_READ));
    assert(calls.empty() && s.protection.size() == 3); // lazy views remain unmapped
    std::puts("PASS: exact spans, mixed protections, chunk limits, failure bookkeeping, lazy views");
}
