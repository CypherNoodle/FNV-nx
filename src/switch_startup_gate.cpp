#include "fnv_xbox_pch.h"
#include "switch_call_profile.h"
#include "switch_ring_progress.h"
#include "ring_progress_gate.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <rex/cvar.h>

// Opt-in workaround for the ordering observed in the Windows Xbox reference.
// The original guest implementations remain available under __imp__ names.
REXCVAR_DEFINE_BOOL(fnv_startup_barrier, false, "FNV",
                   "Experimental wait for the observed main initialization checkpoint");
REXCVAR_DEFINE_BOOL(switch_profile_stacks, false, "FNV",
                   "Diagnostic SDK stack sampling; pauses threads and affects timing");
REXCVAR_DEFINE_BOOL(fnv_profile_lowfps, false, "FNV",
                   "Sample one 60-second low-FPS gameplay interval, then return to passive profiling");
REXCVAR_DEFINE_BOOL(fnv_gpu_wait_backoff, false, "FNV",
                   "Experimental 50us backoff in the verified unchanged-progress GPU poll loop");
REXCVAR_DEFINE_BOOL(fnv_save_trace, false, "FNV",
                   "Diagnostic entry/exit trace of original save open and stream wait paths");

namespace {
std::atomic<bool> ready{false};
uint32_t ReadGuestWord(uint8_t* base, uint32_t address) {
    return __builtin_bswap32(*reinterpret_cast<const volatile uint32_t*>(base + address));
}
}

REX_EXTERN(__imp__sub_82557648);
REX_EXTERN(__imp__sub_82244E48);
REX_EXTERN(__imp__sub_82C57A38);
REX_EXTERN(__imp__sub_82C57A48);
REX_EXTERN(__imp__sub_8278FC10);
REX_EXTERN(__imp__sub_825F67F8);
REX_EXTERN(__imp__sub_825FF6A8);
REX_EXTERN(__imp__sub_825F5528);

REX_EXTERN(sub_825FF6A8) {
    const bool trace = REXCVAR_GET(fnv_save_trace);
    if (trace) REXLOG_INFO("FNV save container enter: caller={:08X} r3={:08X} r4={:08X}",
                           static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r4.u32);
    __imp__sub_825FF6A8(ctx, base);
    if (trace) REXLOG_INFO("FNV save container return: result={:08X}", ctx.r3.u32);
}

REX_EXTERN(sub_825F5528) {
    const bool trace = REXCVAR_GET(fnv_save_trace);
    if (trace) REXLOG_INFO("FNV save stream wait enter: caller={:08X} object={:08X}",
                           static_cast<uint32_t>(ctx.lr), ctx.r3.u32);
    __imp__sub_825F5528(ctx, base);
    if (trace) REXLOG_INFO("FNV save stream wait return: result={:08X}", ctx.r3.u32);
}

REX_EXTERN(sub_825F67F8) {
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
    const uint32_t manager = ctx.r3.u32;
    const uint32_t before = ReadGuestWord(base, manager + 580);
    __imp__sub_825F67F8(ctx, base);
    const uint32_t after = ReadGuestWord(base, manager + 580);
    REXLOG_INFO("FNV guest save phase: caller={:08X} manager={:08X} flags_before={:08X} flags_after={:08X} result={:08X}",
                caller, manager, before, after, ctx.r3.u32);
}

REX_EXTERN(sub_8278FC10) {
    // Only the wait loop in 8224AC98; other uses keep their original timing.
    const bool backoff = REXCVAR_GET(fnv_gpu_wait_backoff);
    const bool enabled = (backoff || RexFnvRingProgressEnabled()) &&
                         static_cast<uint32_t>(ctx.lr) == 0x8224AD30;
    const uint32_t state = ctx.r3.u32;
    const uint32_t before = enabled ? ReadGuestWord(base, state + 8) : 0;
    __imp__sub_8278FC10(ctx, base);
    if (!enabled) return;
    struct PollState {
        uint32_t address = 0;
        unsigned unchanged = 0;
        uint64_t fallback = 0, changed = 0, timed_out = 0;
        std::chrono::steady_clock::time_point last_report{};
    };
    static thread_local PollState poll;
    if (poll.address != state || ctx.r3.u32 != 1 ||
        ReadGuestWord(base, state + 8) != before) {
        poll.address = state;
        poll.unchanged = 0;
        return;
    }
    if (++poll.unchanged < 32) return;
    poll.unchanged = 0;
    unsigned notified = 0;
    // The original has just read this exact pointer. A known physical alias
    // must match the SDK's writeback address; arbitrary virtual pointers fall back.
    const uint32_t device = ReadGuestWord(base, state);
    const uint32_t guest_pointer = ReadGuestWord(base, device + 10896);
    uint32_t physical_pointer = 0;
    {
        FNV_CALL_SCOPE(17);
        if (fnv::RingPhysicalAddress(guest_pointer, physical_pointer)) {
            notified = RexFnvRingProgressWait(physical_pointer, before);
        }
        if (!notified && backoff) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            ++poll.fallback;
        }
    }
    if (notified == 1) ++poll.changed;
    if (notified == 2) ++poll.timed_out;
    // Wakeups only permit another original poll. Keep its return and timeout.
    const auto now = std::chrono::steady_clock::now();
    if (poll.last_report == std::chrono::steady_clock::time_point{}) poll.last_report = now;
    if (now - poll.last_report >= std::chrono::seconds(10)) {
        const double seconds = std::chrono::duration<double>(now - poll.last_report).count();
        REXLOG_INFO("FNV GPU ring wait: caller=8224AD30 pointer={:08X} interval_s={:.3f} "
                    "progress={} timeout250us={} fallback50us={} (interval counts; not GPU completion)",
                    guest_pointer, seconds, poll.changed, poll.timed_out, poll.fallback);
        REXLOG_INFO("FNV ring mapping: virtual={:08X} physical={:08X} (SDK E alias includes +1000)",
                    guest_pointer, physical_pointer);
        poll.changed = poll.timed_out = poll.fallback = 0;
        poll.last_report = now;
    }
}

REX_EXTERN(__imp__sub_8224AC98);
REX_EXTERN(sub_8224AC98) {
    FNV_CALL_SCOPE(12);
    __imp__sub_8224AC98(ctx, base);
}

// Observe original XDK wrappers; preserve their calls, results and PPC state.
REX_EXTERN(sub_82C57A38) {
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
    const uint32_t overlapped = ctx.r4.u32;
    __imp__sub_82C57A38(ctx, base);
    static std::atomic<unsigned> count{0};
    if (count.fetch_add(1, std::memory_order_relaxed) < 64) {
        REXLOG_INFO("FNV storage close caller={:08X} result={:08X} overlapped={:08X} completion={:08X} extended={:08X}",
                    caller, ctx.r3.u32, overlapped,
                    overlapped ? ReadGuestWord(base, overlapped) : 0,
                    overlapped ? ReadGuestWord(base, overlapped + 24) : 0);
    }
}

REX_EXTERN(sub_82C57A48) {
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
    const uint32_t device = ctx.r3.u32;
    const uint32_t overlapped = ctx.r4.u32;
    __imp__sub_82C57A48(ctx, base);
    static std::atomic<unsigned> count{0};
    if (count.fetch_add(1, std::memory_order_relaxed) < 64 ||
        (ctx.r3.u32 != 0 && ctx.r3.u32 != 997)) {
        REXLOG_INFO("FNV storage state caller={:08X} device={:08X} result={:08X} overlapped={:08X}",
                    caller, device, ctx.r3.u32, overlapped);
    }
}

REX_EXTERN(sub_82557648) {
    if (REXCVAR_GET(fnv_startup_barrier) && !ready.load(std::memory_order_acquire)) {
        REXLOG_INFO("FNV startup barrier: worker waiting for main checkpoint");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (!ready.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready.load(std::memory_order_acquire)) {
            REXLOG_WARN("FNV startup barrier timed out; continuing original worker");
        }
    }
    __imp__sub_82557648(ctx, base);
}

REX_EXTERN(sub_82244E48) {
    if (REXCVAR_GET(fnv_startup_barrier) && static_cast<uint32_t>(ctx.lr) == 0x8261740C &&
        ReadGuestWord(base, 0x8308F74C) != 0 && ReadGuestWord(base, 0x8308F788) != 0 &&
        !ready.exchange(true, std::memory_order_acq_rel)) {
        REXLOG_INFO("FNV startup barrier: observed main checkpoint, worker released");
    }
    __imp__sub_82244E48(ctx, base);
}
