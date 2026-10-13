#include "fnv_xbox_pch.h"
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
