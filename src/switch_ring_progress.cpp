#include "ring_progress_gate.h"
#include "switch_ring_progress.h"
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(fnv_gpu_wait_notify, false, "FNV",
                   "Bounded wait for the verified ring read-pointer writeback, not GPU completion");

namespace {
fnv::RingProgressGate& Gate() {
    static fnv::RingProgressGate gate;
    return gate;
}
}
extern "C" bool RexFnvRingProgressEnabled() { return REXCVAR_GET(fnv_gpu_wait_notify); }
extern "C" void RexFnvRingProgressPublish(uint32_t address, uint32_t value) {
    if (REXCVAR_GET(fnv_gpu_wait_notify)) Gate().Publish(address & 0x1FFFFFFFu, value);
}
extern "C" void RexFnvRingProgressInvalidate() {
    if (REXCVAR_GET(fnv_gpu_wait_notify)) Gate().Invalidate();
}
extern "C" unsigned RexFnvRingProgressWait(uint32_t address, uint32_t observed) {
    if (!REXCVAR_GET(fnv_gpu_wait_notify)) return 0;
    switch (Gate().Wait(address, observed, std::chrono::microseconds(250))) {
        case fnv::RingProgressGate::Result::Changed: return 1;
        case fnv::RingProgressGate::Result::TimedOut: return 2;
        default: return 0;
    }
}
