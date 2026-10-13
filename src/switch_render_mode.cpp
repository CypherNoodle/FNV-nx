#include "fnv_xbox_pch.h"
#include <rex/cvar.h>
#include "render_mode_guard.h"

REXCVAR_DEFINE_BOOL(fnv_fixed_render_mode, false, "FNV",
                   "Experimental fixed renderer creation size at the audited bootstrap call");
REXCVAR_DEFINE_INT32(fnv_render_width, 960, "FNV", "Requested fixed renderer width")
    .range(640, 1280).lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_INT32(fnv_render_height, 540, "FNV", "Requested fixed renderer height")
    .range(480, 720).lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REX_EXTERN(__imp__sub_827B5D98);

namespace {
constexpr uint32_t kWidth = 0x82FFF304;
constexpr uint32_t kHeight = 0x82FFF308;
uint32_t Read(uint8_t* base, uint32_t address) {
    return __builtin_bswap32(*reinterpret_cast<const volatile uint32_t*>(base + address));
}
void Write(uint8_t* base, uint32_t address, uint32_t value) {
    *reinterpret_cast<volatile uint32_t*>(base + address) = __builtin_bswap32(value);
}
}

REX_EXTERN(sub_827B5D98) {
    if (!REXCVAR_GET(fnv_fixed_render_mode)) {
        __imp__sub_827B5D98(ctx, base);
        return;
    }
    const uint32_t caller = static_cast<uint32_t>(ctx.lr);
    const uint32_t width = ctx.r3.u32, height = ctx.r4.u32;
    const uint32_t requested_width = uint32_t(REXCVAR_GET(fnv_render_width));
    const uint32_t requested_height = uint32_t(REXCVAR_GET(fnv_render_height));
    // Do not access the audited globals on unrelated calls.
    if (caller != 0x8230F3F0) {
        __imp__sub_827B5D98(ctx, base);
        return;
    }
    const uint32_t global_width = Read(base, kWidth), global_height = Read(base, kHeight);
    if (!FnvCanOverrideRenderMode(caller, width, height, global_width, global_height,
                                 requested_width, requested_height)) {
        REXLOG_WARN("FNV renderer mode guard rejected: args={}x{} globals={}x{} requested={}x{}",
                    width, height, global_width, global_height, requested_width, requested_height);
        __imp__sub_827B5D98(ctx, base);
        return;
    }
    Write(base, kWidth, requested_width);
    Write(base, kHeight, requested_height);
    ctx.r3.u64 = requested_width;
    ctx.r4.u64 = requested_height;
    REXLOG_INFO("FNV renderer creation override: {}x{} -> {}x{} at caller {:08X}",
                width, height, requested_width, requested_height, caller);
    __imp__sub_827B5D98(ctx, base);
    if (!ctx.r3.u32) {
        Write(base, kWidth, global_width);
        Write(base, kHeight, global_height);
        REXLOG_ERROR("FNV renderer creation failed; restored bootstrap dimension globals");
    } else {
        REXLOG_INFO("FNV renderer created: guest object={:08X}, dimension globals={}x{}; verify actual GPU targets",
                    ctx.r3.u32, Read(base, kWidth), Read(base, kHeight));
    }
}
