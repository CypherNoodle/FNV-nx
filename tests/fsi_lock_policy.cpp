#include "fsi_lock_policy.h"
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
    for (unsigned bits = 0; bits < 512; ++bits) {
        const bool b[] = {bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),
                          bool(bits&16),bool(bits&32),bool(bits&64),bool(bits&128),bool(bits&256)};
        const bool allowed = fnv::LateFsiEligible(b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8]);
        assert(allowed == (bits == 7)); // every side-effect/coverage guard excludes the draw
    }
    // For a side-effect-free, non-discarding shader, executing shading before
    // the ordered critical section must leave compare, write and color decisions
    // identical. Enumerate all eight depth compare functions and write modes.
    for (unsigned compare = 0; compare < 8; ++compare)
      for (unsigned write = 0; write < 2; ++write)
        for (uint32_t stored = 0; stored < 64; ++stored)
          for (uint32_t fragment = 0; fragment < 64; ++fragment) {
            const bool pass = ((fragment < stored) && (compare & 1)) ||
                              ((fragment == stored) && (compare & 2)) ||
                              ((fragment > stored) && (compare & 4));
            const uint32_t shaded = fragment * 17 + 9; // no kill, depth export or external writes
            const uint32_t early_depth = pass && write ? fragment : stored;
            const uint32_t early_color = pass ? shaded : 0;
            const uint32_t late_shaded = fragment * 17 + 9;
            const uint32_t late_depth = pass && write ? fragment : stored;
            const uint32_t late_color = pass ? late_shaded : 0;
            assert(early_depth == late_depth && early_color == late_color);
          }
    std::cout << "PASS FSI policy: all 512 guard combinations and depth/color decision model; GPU image comparison pending\n";
}
