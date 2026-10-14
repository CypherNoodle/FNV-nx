#include "switch_fps.h"
#include "switch_graphics_trace.h"
#include "fps_glyphs.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(fnv_show_fps, true, "FNV", "Small guest-frame FPS counter on the final host image");
namespace {
std::atomic<unsigned> fps_tenths{0};
}
extern "C" void RexFnvFpsFrame() {
    RexFnvGraphicsFrame();
    // Single producer: the existing IssueSwap counter on GPU Commands.
    static auto start = std::chrono::steady_clock::now();
    static uint64_t frames = 0;
    ++frames;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - start).count();
    if (elapsed >= 1.0) {
        fps_tenths.store(static_cast<unsigned>(std::min(9999.0, frames * 10.0 / elapsed + 0.5)),
                         std::memory_order_relaxed);
        frames = 0;
        start = now;
    }
}
extern "C" void RexFnvFpsDraw(VkCommandBuffer commands, uint32_t width, uint32_t height,
                             PFN_vkCmdClearAttachments clear) {
    if (!REXCVAR_GET(fnv_show_fps) || !clear || width < 180 || height < 40) return;
    // Reuse the presenter's open color pass. No ImGui drawer, separate submit,
    // shaders, textures or resource lifetime changes.
    char text[24]{};
    const unsigned tenths = fps_tenths.load(std::memory_order_relaxed);
    std::snprintf(text, sizeof(text), "FPS: %u.%u", tenths / 10, tenths % 10);
    VkClearAttachment attachment{};
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.colorAttachment = 0;
    attachment.clearValue.color.float32[3] = 1.0f;
    VkClearRect background{};
    background.rect.offset = {12, 12};
    background.rect.extent = {uint32_t(std::strlen(text)) * 12 + 8, 22};
    background.layerCount = 1;
    clear(commands, 1, &attachment, 1, &background);
    VkClearRect rectangles[256]{};
    unsigned count = 0;
    fnv::BuildFpsGlyphs(text, [&](const fnv::FpsRect& r) {
        if (count == 256 || r.x + r.width > width || r.y + r.height > height) return;
        auto& rect = rectangles[count++];
        rect.rect.offset = {int32_t(r.x), int32_t(r.y)};
        rect.rect.extent = {r.width, r.height};
        rect.layerCount = 1;
    });
    attachment.clearValue.color.float32[0] = 1.0f;
    attachment.clearValue.color.float32[1] = 1.0f;
    attachment.clearValue.color.float32[2] = 0.3f;
    if (count) clear(commands, 1, &attachment, count, rectangles);
}
