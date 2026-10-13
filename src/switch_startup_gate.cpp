#include "fnv_xbox_pch.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <rex/cvar.h>

// Opt-in workaround for the ordering observed in the Windows Xbox reference.
// The original guest implementations remain available under __imp__ names.
REXCVAR_DEFINE_BOOL(fnv_startup_barrier, false, "FNV",
                   "Experimental wait for the observed main initialization checkpoint");

namespace {
std::atomic<bool> ready{false};
uint32_t ReadGuestWord(uint8_t* base, uint32_t address) {
    return __builtin_bswap32(*reinterpret_cast<const volatile uint32_t*>(base + address));
}
}

REX_EXTERN(__imp__sub_82557648);
REX_EXTERN(__imp__sub_82244E48);

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
