#pragma once
// Inclusive host wall time, including waits. Not GPU execution time.
#include <rex/platform.h>
#if REX_PLATFORM_SWITCH
#include <switch.h>
#include <cstdio>
extern "C" bool RexFnvCallProfileEnabled();
extern "C" void RexFnvCallProfileAdd(unsigned id, uint64_t ticks);
extern "C" void RexFnvCallProfileReport(FILE* file, double seconds, double frames);
extern "C" void RexFnvThreadCoreReport(FILE* file, Handle thread, uint64_t elapsed_ticks);
class FnvCallScope {
    unsigned id_;
    uint64_t start_;
    bool enabled_;
public:
    explicit FnvCallScope(unsigned id) : id_(id), start_(0),
        enabled_(RexFnvCallProfileEnabled()) {
        if (enabled_) start_ = armGetSystemTick();
    }
    ~FnvCallScope() {
        if (enabled_) RexFnvCallProfileAdd(id_, armGetSystemTick() - start_);
    }
    FnvCallScope(const FnvCallScope&) = delete;
    FnvCallScope& operator=(const FnvCallScope&) = delete;
};
#define FNV_CALL_SCOPE(id) FnvCallScope fnv_call_scope_##id(id)
#else
#define FNV_CALL_SCOPE(id) ((void)0)
#endif
