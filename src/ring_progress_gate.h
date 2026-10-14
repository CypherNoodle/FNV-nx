#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace fnv {
// A notification of a read-pointer store, never a host GPU completion fence.
class RingProgressGate {
 public:
    enum class Result { Unsupported, Changed, TimedOut };

    void Invalidate() {
        std::lock_guard lock(mutex_);
        valid_ = false;
        ++generation_;
        if (waiters_) changed_.notify_all();
    }

    void Publish(uint32_t physical_address, uint32_t value) {
        std::lock_guard lock(mutex_);
        const bool changed = !valid_ || address_ != physical_address || value_ != value;
        if (!valid_ || address_ != physical_address) ++generation_;
        address_ = physical_address;
        value_ = value;
        valid_ = true;
        if (changed && waiters_) changed_.notify_all();
    }

    Result Wait(uint32_t physical_address, uint32_t observed,
                std::chrono::microseconds limit) {
        std::unique_lock lock(mutex_);
        if (!valid_ || address_ != physical_address) return Result::Unsupported;
        if (value_ != observed) return Result::Changed;
        const auto generation = generation_;
        ++waiters_;
        const bool woke = changed_.wait_for(lock, limit, [&] {
            return !valid_ || generation_ != generation || value_ != observed;
        });
        --waiters_;
        if (!valid_ || generation_ != generation) return Result::Unsupported;
        return woke ? Result::Changed : Result::TimedOut;
    }

 private:
    std::mutex mutex_;
    std::condition_variable changed_;
    uint32_t address_ = 0, value_ = 0;
    uint64_t generation_ = 0;
    unsigned waiters_ = 0;
    bool valid_ = false;
};

// Only the guest physical alias windows are comparable to TranslatePhysical.
inline bool RingPhysicalAddress(uint32_t guest, uint32_t& physical) {
    const uint32_t window = guest & 0xE0000000u;
    if ((window != 0xA0000000u && window != 0xC0000000u && window != 0xE0000000u) ||
        (guest & 3u)) return false;
    physical = guest & 0x1FFFFFFFu;
    return true;
}
}
