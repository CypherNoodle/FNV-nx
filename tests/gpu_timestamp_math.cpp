#include "gpu_timestamp_math.h"
#include <cassert>
#include <iostream>
int main() {
    assert(fnv::TimestampDelta(10, 100, 64) == 90);
    assert(fnv::TimestampDelta(UINT64_MAX - 4, 5, 64) == 10);
    assert(fnv::TimestampDelta(250, 4, 8) == 10);
    assert(fnv::TimestampDelta(0x1FA, 0x204, 8) == 10);
    assert(fnv::TimestampDelta(0, 100, 0) == 0);
    assert(fnv::TimestampDelta(0, 100, 65) == 0);
    std::cout << "PASS GPU timestamps: supported width and wrap handling\n";
}
