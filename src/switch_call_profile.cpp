#include "switch_call_profile.h"
#include <atomic>
#include <rex/cvar.h>
#include <rex/chrono/clock.h>

REXCVAR_DEFINE_BOOL(fnv_call_profile, false, "FNV",
                   "Diagnostic inclusive CPU call/wait timings; not GPU timestamps");
namespace {
struct Counters {
    std::atomic<uint64_t> calls{0}, ticks{0}, maximum{0};
};
constexpr const char* names[] = {
    "draw-inclusive", "resolve-inclusive", "submit-inclusive", "begin-submission-inclusive",
    "fence-block", "fence-poll", "textures-inclusive", "shared-ranges-inclusive",
    "pipeline-config-inclusive", "pm4-wait-reg-mem", "pm4-interrupt",
    "render-target-update-inclusive", "guest-gpu-wait-inclusive", "sampler-reuse-inclusive",
    "memexport-readback-full", "memexport-readback-fast", "resolve-readback-inclusive",
    "guest-poll-backoff-sleep", "gpu-command-idle", "pm4-buffer-inclusive",
    "interrupt-callback-inclusive", "memory-protect-inclusive", "swap-inclusive",
    "vblank-callback-inclusive", "guest-file-read-inclusive", "guest-file-write-inclusive"
};
Counters counters[sizeof(names) / sizeof(names[0])];
}
extern "C" bool RexFnvCallProfileEnabled() { return REXCVAR_GET(fnv_call_profile); }
extern "C" void RexFnvCallProfileAdd(unsigned id, uint64_t ticks) {
    if (id >= sizeof(names) / sizeof(names[0])) return;
    auto& c = counters[id];
    c.ticks.fetch_add(ticks, std::memory_order_relaxed);
    c.calls.fetch_add(1, std::memory_order_relaxed);
    auto old = c.maximum.load(std::memory_order_relaxed);
    while (old < ticks && !c.maximum.compare_exchange_weak(old, ticks, std::memory_order_relaxed)) {}
}
extern "C" void RexFnvCallProfileReport(FILE* file, double seconds, double frames) {
    if (!RexFnvCallProfileEnabled() || seconds < 0.5) return;
    // Two observations per report, not per draw. Compare the running guest clock
    // against Horizon's counter; do not change the guest scalar or timer state.
    static uint64_t last_host = 0, last_guest = 0;
    const uint64_t host_now = armGetSystemTick();
    const uint64_t guest_now = rex::chrono::Clock::QueryGuestTickCount();
    const uint64_t host_hz = armGetSystemTickFreq();
    const uint64_t guest_hz = rex::chrono::Clock::guest_tick_frequency();
    if (last_host && host_now > last_host && guest_now >= last_guest && guest_hz) {
        const double host_s = double(host_now - last_host) / host_hz;
        const double guest_s = double(guest_now - last_guest) / guest_hz;
        std::fprintf(file, "     FNV clock: host_hz=%llu guest_hz=%llu scalar=%.6f "
                           "host_elapsed=%.6f s guest_elapsed=%.6f s ratio=%.6f "
                           "vblank_per_swap=%s clock_no_scaling=%s clock_source_raw=%s renderer=%s\n",
                     (unsigned long long)host_hz, (unsigned long long)guest_hz,
                     rex::chrono::Clock::guest_time_scalar(), host_s, guest_s, guest_s / host_s,
                     rex::cvar::GetFlagByName("vblank_per_swap").c_str(),
                     rex::cvar::GetFlagByName("clock_no_scaling").c_str(),
                     rex::cvar::GetFlagByName("clock_source_raw").c_str(),
                     rex::cvar::GetFlagByName("render_target_path_vulkan").c_str());
    }
    last_host = host_now;
    last_guest = guest_now;
    std::fprintf(file, "     FNV call timings: inclusive host wall time; nested rows overlap; "
                       "wait time is NOT GPU execution time; report-boundary counters approximate\n");
    for (unsigned id = 0; id < sizeof(names) / sizeof(names[0]); ++id) {
        auto& c = counters[id];
        const auto calls = c.calls.exchange(0, std::memory_order_relaxed);
        const auto ticks = c.ticks.exchange(0, std::memory_order_relaxed);
        const auto maximum = c.maximum.exchange(0, std::memory_order_relaxed);
        if (!calls) continue;
        const double ms = double(armTicksToNs(ticks)) / 1e6;
        std::fprintf(file, "       %s: %llu calls | %.2f calls/frame | %.3f ms/frame | "
                           "%.3f ms/s | mean %.4f ms | max %.3f ms\n",
                     names[id], (unsigned long long)calls, frames > 0 ? calls / frames : 0,
                     frames > 0 ? ms / frames : 0, ms / seconds, ms / calls,
                     double(armTicksToNs(maximum)) / 1e6);
    }
}

extern "C" void RexFnvThreadCoreReport(FILE* file, Handle thread, uint64_t elapsed_ticks) {
    if (!RexFnvCallProfileEnabled() || elapsed_ticks < armGetSystemTickFreq() / 2) return;
    // Profiler-thread-only, bounded state. Measures this thread's execution on
    // each core, not system-wide idle time and not its permitted affinity mask.
    struct ThreadCores { Handle handle = 0; uint64_t previous[4]{}; bool valid[4]{}; };
    static ThreadCores slots[128];
    ThreadCores* slot = nullptr;
    for (auto& candidate : slots) {
        if (candidate.handle == thread || !candidate.handle) { slot = &candidate; break; }
    }
    if (!slot) { std::fprintf(file, "   FNV per-core: tracking table full\n"); return; }
    slot->handle = thread;
    uint64_t current[4]{}, total = 0;
    Result results[4]{};
    bool all_zero = true, all_ok = true;
    for (unsigned core = 0; core < 4; ++core) {
        results[core] = svcGetInfo(&current[core], InfoType_ThreadTickCount, thread, core);
        all_ok &= R_SUCCEEDED(results[core]);
        all_zero &= current[core] == 0;
    }
    if (all_ok && all_zero && R_SUCCEEDED(svcGetInfo(&total, InfoType_ThreadTickCount,
                                                   thread, TickCountInfo_Total)) && total) {
        std::fprintf(file, "   FNV per-core: unavailable/inconsistent; all core counters zero "
                           "while total thread ticks are nonzero; use total CPU and affinity\n");
        for (auto& valid : slot->valid) valid = false;
        return;
    }
    std::fprintf(file, "   FNV per-core CPU (thread execution; 100%% = one core):");
    for (unsigned core = 0; core < 4; ++core) {
        const uint64_t ticks = current[core];
        const Result rc = results[core];
        if (R_FAILED(rc)) {
            slot->valid[core] = false;
            std::fprintf(file, " core%u=unavailable(0x%X)", core, rc);
            continue;
        }
        if (slot->valid[core] && ticks >= slot->previous[core]) {
            std::fprintf(file, " core%u=%.2f%%", core,
                         double(ticks - slot->previous[core]) * 100.0 / elapsed_ticks);
        } else { std::fprintf(file, " core%u=baseline", core); }
        slot->previous[core] = ticks;
        slot->valid[core] = true;
    }
    std::fprintf(file, "\n");
}
