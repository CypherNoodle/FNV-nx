#include "ring_progress_gate.h"
#include <cassert>
#include <chrono>
#include <future>
#include <iostream>

using namespace std::chrono_literals;
using Gate = fnv::RingProgressGate;
using Result = Gate::Result;
int main() {
    uint32_t physical = 0;
    assert(fnv::RingPhysicalAddress(0xA1234000, physical) && physical == 0x01234000);
    assert(fnv::RingPhysicalAddress(0xC1234000, physical) && physical == 0x01234000);
    assert(fnv::RingPhysicalAddress(0xE1234000, physical) && physical == 0x01234000);
    assert(!fnv::RingPhysicalAddress(0x81234000, physical));
    assert(!fnv::RingPhysicalAddress(0xA1234001, physical));
    Gate gate;
    assert(gate.Wait(16, 8, 1ms) == Result::Unsupported);
    gate.Publish(16, 8);
    assert(gate.Wait(20, 8, 1ms) == Result::Unsupported);
    assert(gate.Wait(16, 4, 1ms) == Result::Changed); // publication before parking
    assert(gate.Wait(16, 8, 1ms) == Result::TimedOut);
    // Repeated publication of an unchanged value must not manufacture progress.
    auto repeated = std::async(std::launch::async, [&] { return gate.Wait(16, 8, 10ms); });
    for (unsigned i = 0; i < 100; ++i) gate.Publish(16, 8);
    assert(repeated.get() == Result::TimedOut);
    auto progress = std::async(std::launch::async, [&] { return gate.Wait(16, 8, 1s); });
    gate.Publish(16, 0); // ring wrap: inequality, not monotonic greater-than
    assert(progress.get() == Result::Changed);
    auto other = std::async(std::launch::async, [&] { return gate.Wait(16, 0, 1s); });
    gate.Publish(20, 0);
    assert(other.get() == Result::Unsupported);
    auto stop = std::async(std::launch::async, [&] { return gate.Wait(20, 0, 1s); });
    gate.Invalidate();
    assert(stop.get() == Result::Unsupported);
    std::cout << "PASS: exact address, bounded timeout, unchanged store, prepublished progress, wrap, reconfigure, stop\n";
}
