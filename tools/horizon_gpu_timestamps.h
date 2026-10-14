#pragma once
#include <array>
#include <vector>
#include <rex/ui/vulkan/device.h>
#include <rex/graphics/vulkan/deferred_command_buffer.h>
#include <rex/logging.h>
#include "fnv_gpu_timestamp_math.h"
extern "C" bool RexFnvGpuTimestampsEnabled();

namespace fnv {
// GPU Commands only. Read results only for submissions already retired by the
// SDK's original fences. Never wait for a query or reuse an in-flight slot.
class GpuTimestampProbe {
    static constexpr unsigned kSlots = 4, kQueries = 64;
    struct Sample {
        uint64_t submission = 0;
        unsigned count = 0, category = 0;
        bool pending = false, ended = false;
        std::array<unsigned, kQueries> spans{};
    };
    std::array<Sample, kSlots> samples_{};
    VkQueryPool pool_ = VK_NULL_HANDLE;
    PFN_vkCmdWriteTimestamp write_ = nullptr;
    PFN_vkGetQueryPoolResults get_ = nullptr;
    const rex::ui::vulkan::VulkanDevice* device_ = nullptr;
    int active_ = -1;
    unsigned bits_ = 0, sequence_ = 0;
    double period_ = 0;
    bool attempted_ = false;

    bool Initialize(const rex::ui::vulkan::VulkanDevice* device) {
        if (attempted_) return pool_ != VK_NULL_HANDLE;
        attempted_ = true;
        device_ = device;
        const auto& ifn = device->vulkan_instance()->functions();
        uint32_t count = 0;
        ifn.vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(), &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        ifn.vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(), &count, queues.data());
        const unsigned family = device->queue_family_graphics_compute();
        if (family >= count || !queues[family].timestampValidBits) {
            REXGPU_WARN("FNV GPU timestamps unavailable on graphics queue");
            return false;
        }
        bits_ = queues[family].timestampValidBits;
        VkPhysicalDeviceProperties properties{};
        ifn.vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
        period_ = properties.limits.timestampPeriod;
        write_ = reinterpret_cast<PFN_vkCmdWriteTimestamp>(
            ifn.vkGetDeviceProcAddr(device->device(), "vkCmdWriteTimestamp"));
        get_ = reinterpret_cast<PFN_vkGetQueryPoolResults>(
            ifn.vkGetDeviceProcAddr(device->device(), "vkGetQueryPoolResults"));
        if (!write_ || !get_ || period_ <= 0 || bits_ > 64) return false;
        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = kSlots * kQueries;
        if (device->functions().vkCreateQueryPool(device->device(), &info, nullptr, &pool_) != VK_SUCCESS) {
            REXGPU_WARN("FNV GPU timestamp pool unavailable; rendering continues");
            return false;
        }
        REXGPU_INFO("FNV GPU timestamps enabled: valid_bits={} period_ns={} one_in_16_submissions "
                    "4_slots 64_markers; group spans include dependencies/uploads; not pure shader cost",
                    bits_, period_);
        return true;
    }
    void Stamp(rex::graphics::vulkan::DeferredCommandBuffer& commands, unsigned category) {
        if (active_ < 0) return;
        auto& s = samples_[active_];
        if (s.ended || s.count >= kQueries) return;
        s.spans[s.count] = category;
        commands.CmdFnvWriteTimestamp(write_, pool_, unsigned(active_) * kQueries + s.count++);
    }
 public:
    void Collect(uint64_t completed) {
        if (!pool_) return;
        for (unsigned slot = 0; slot < kSlots; ++slot) {
            auto& s = samples_[slot];
            if (!s.pending || s.submission > completed) continue;
            std::array<uint64_t, kQueries> values{};
            const VkResult result = get_(device_->device(), pool_, slot * kQueries, s.count,
                                         sizeof(values), values.data(), sizeof(uint64_t),
                                         VK_QUERY_RESULT_64_BIT); // deliberately no WAIT/PARTIAL
            if (result == VK_NOT_READY) continue;
            if (result == VK_SUCCESS) {
                double ms[3]{};
                for (unsigned i = 1; i < s.count; ++i)
                    ms[s.spans[i - 1]] += TimestampDelta(values[i - 1], values[i], bits_) * period_ / 1e6;
                REXGPU_INFO("FNV GPU sample: submission={} groups={} draw_ms={:.3f} resolve_ms={:.3f} "
                            "other_ms={:.3f} total_ms={:.3f} (sparse sample; GPU ordered spans)",
                            s.submission, s.count - 1, ms[1], ms[2], ms[0], ms[0]+ms[1]+ms[2]);
            } else {
                REXGPU_WARN("FNV GPU query read failed: {}; original GPU completion retained", int(result));
            }
            s.pending = false;
        }
    }
    void Begin(const rex::ui::vulkan::VulkanDevice* device,
               rex::graphics::vulkan::DeferredCommandBuffer& commands, uint64_t completed) {
        Collect(completed);
        active_ = -1;
        if (!RexFnvGpuTimestampsEnabled() || (++sequence_ % 16) != 0 || !Initialize(device)) return;
        for (unsigned slot = 0; slot < kSlots; ++slot) {
            if (samples_[slot].pending) continue;
            active_ = int(slot);
            samples_[slot] = {};
            commands.CmdVkResetQueryPool(pool_, slot * kQueries, kQueries);
            Stamp(commands, 0);
            break;
        }
    }
    void Category(rex::graphics::vulkan::DeferredCommandBuffer& commands, unsigned category) {
        if (active_ < 0) return;
        auto& s = samples_[active_];
        if (s.ended || category == s.category || s.count >= kQueries - 1) return;
        Stamp(commands, category);
        s.category = category;
    }
    void Finish(rex::graphics::vulkan::DeferredCommandBuffer& commands) {
        if (active_ < 0 || samples_[active_].ended) return;
        Stamp(commands, 0);
        samples_[active_].ended = true;
    }
    void Commit(uint64_t submission) {
        if (active_ < 0) return;
        auto& s = samples_[active_];
        s.pending = true;
        s.submission = submission;
        active_ = -1;
    }
    // Only after the original AwaitAllQueueOperationsCompletion shutdown path.
    void Shutdown() {
        if (pool_) {
            device_->functions().vkDestroyQueryPool(device_->device(), pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }
};
}
