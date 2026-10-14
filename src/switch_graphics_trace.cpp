#include "switch_graphics_trace.h"
#include <array>
#include <atomic>
#include <cstring>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/device.h>

REXCVAR_DEFINE_BOOL(fnv_graphics_trace, false, "FNV",
                   "Bounded render-target/attachment diagnostics and deferred pass/barrier counters");
namespace {
std::atomic<uint64_t> legacy{0}, dynamic{0}, barriers{0}, memory_items{0}, buffer_items{0}, image_items{0}, broad{0};
std::atomic<uint64_t> states{0}, transitions{0}, fsi_states{0}, fbo_states{0};
std::atomic<uint64_t> frame_id{0};
std::atomic<uint64_t> depth_export_fbo_float{0}, depth_export_fbo_fixed{0}, depth_export_fsi{0};
std::atomic<uint64_t> depth_export_remap{0};
template<class T> uint64_t Bits(T value) {
    uint64_t bits = 0;
    static_assert(sizeof(value) <= sizeof(bits));
    std::memcpy(&bits, &value, sizeof(value));
    return bits;
}
// Written only by GPU Commands. The profiler reads only atomic counters.
template<size_t N, size_t Capacity> bool First(std::array<std::array<uint64_t,N>,Capacity>& seen,
                                            unsigned& count, const std::array<uint64_t,N>& key) {
    for (unsigned i=0;i<count;++i) if (seen[i]==key) return false;
    if (count==Capacity) return false;
    seen[count++]=key;
    return true;
}
}
extern "C" bool RexFnvGraphicsTraceEnabled() { return REXCVAR_GET(fnv_graphics_trace); }
extern "C" void RexFnvGraphicsFrame() { frame_id.fetch_add(1,std::memory_order_relaxed); }
extern "C" uint64_t RexFnvGraphicsFrameId() { return frame_id.load(std::memory_order_relaxed); }
extern "C" void RexFnvGraphicsDepthExport(bool fsi, bool float24, bool conversion, uint64_t shader) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    (fsi ? depth_export_fsi : (float24 ? depth_export_fbo_float : depth_export_fbo_fixed))
        .fetch_add(1, std::memory_order_relaxed);
    if (!fsi && float24 && !conversion) depth_export_remap.fetch_add(1,std::memory_order_relaxed);
    static std::array<std::array<uint64_t,4>,32> seen{};
    static unsigned count=0;
    const std::array<uint64_t,4> key={shader,uint64_t(fsi),uint64_t(float24),uint64_t(conversion)};
    if (First(seen,count,key))
        REXLOG_INFO("FNV explicit depth: shader={:016X} fsi={} float24={} conversion={} "
                    "half_scale_fix={} frame={} (draw attempted; visual effect unverified)",
                    shader,fsi,float24,conversion,!fsi&&float24&&!conversion,RexFnvGraphicsFrameId());
}
extern "C" void RexFnvGraphicsBegin(bool is_dynamic, const void* input) {
    if (!RexFnvGraphicsTraceEnabled() || !input) return;
    (is_dynamic ? dynamic : legacy).fetch_add(1,std::memory_order_relaxed);
    static std::array<std::array<uint64_t,32>,48> seen{};
    static unsigned count=0;
    std::array<uint64_t,32> key{};
    key[0]=is_dynamic;
    if (is_dynamic) {
        const auto& info=*static_cast<const VkRenderingInfo*>(input);
        key[1]=info.renderArea.extent.width; key[2]=info.renderArea.extent.height;
        key[3]=info.colorAttachmentCount; key[4]=info.layerCount; key[5]=info.flags;
        key[27]=uint32_t(info.renderArea.offset.x); key[28]=uint32_t(info.renderArea.offset.y);
        key[29]=info.viewMask;
        for (unsigned i=0;i<4 && i<info.colorAttachmentCount;++i) {
            const auto& a=info.pColorAttachments[i];
            key[6+i*4]=Bits(a.imageView); key[7+i*4]=a.imageLayout;
            key[8+i*4]=a.loadOp; key[9+i*4]=a.storeOp;
        }
        if (info.pDepthAttachment) {
            const auto& a=*info.pDepthAttachment;
            key[22]=Bits(a.imageView); key[23]=a.imageLayout; key[24]=a.loadOp; key[25]=a.storeOp;
            if (a.loadOp==VK_ATTACHMENT_LOAD_OP_CLEAR) {
                key[30]=Bits(a.clearValue.depthStencil.depth);
                key[31]=a.clearValue.depthStencil.stencil;
            }
        }
        if (info.pStencilAttachment) key[26]=Bits(info.pStencilAttachment->imageView);
    } else {
        const auto& info=*static_cast<const VkRenderPassBeginInfo*>(input);
        key[1]=info.renderArea.extent.width; key[2]=info.renderArea.extent.height;
        key[27]=uint32_t(info.renderArea.offset.x); key[28]=uint32_t(info.renderArea.offset.y);
        key[3]=info.clearValueCount; key[6]=Bits(info.renderPass); key[10]=Bits(info.framebuffer);
    }
    if (First(seen,count,key)) {
        REXLOG_INFO("FNV attachment snapshot {}: dynamic={} area={}x{} colors_or_clear={} layers={} flags={} "
                    "color0={:X} layout={} load={} store={} color1={:X} color2={:X} color3={:X} "
                    "depth={:X} layout={} load={} store={} stencil={:X} xy={},{} view_mask={} "
                    "depth_clear_bits={:08X} stencil_clear={} (metadata; not pixel capture)",
                    count,key[0],key[1],key[2],key[3],key[4],key[5],key[6],key[7],key[8],key[9],
                    key[10],key[14],key[18],key[22],key[23],key[24],key[25],key[26],
                    int32_t(key[27]),int32_t(key[28]),key[29],key[30],key[31]);
    }
}
extern "C" void RexFnvGraphicsBarrier(uint32_t src, uint32_t dst, uint32_t memory,
                                     uint32_t buffers, uint32_t images) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    barriers.fetch_add(1,std::memory_order_relaxed);
    memory_items.fetch_add(memory,std::memory_order_relaxed);
    buffer_items.fetch_add(buffers,std::memory_order_relaxed);
    image_items.fetch_add(images,std::memory_order_relaxed);
    if ((src|dst)&VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) broad.fetch_add(1,std::memory_order_relaxed);
}
extern "C" void RexFnvGraphicsState(bool fsi, uint32_t surface, uint32_t depth,
                                   uint32_t control, uint32_t mask, const uint32_t* colors) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    states.fetch_add(1,std::memory_order_relaxed);
    (fsi?fsi_states:fbo_states).fetch_add(1,std::memory_order_relaxed);
    static std::array<uint64_t,9> previous{};
    static bool valid=false;
    static std::array<std::array<uint64_t,9>,48> seen{};
    static unsigned count=0;
    const std::array<uint64_t,9> key={uint64_t(fsi),surface,depth,control,mask,
                                    colors[0],colors[1],colors[2],colors[3]};
    if (!valid || previous!=key) {
        transitions.fetch_add(1,std::memory_order_relaxed); previous=key; valid=true;
    }
    if (First(seen,count,key))
        REXLOG_INFO("FNV target snapshot {}: effective={} surface={:08X} depth={:08X} "
                    "depth_control={:08X} color_mask={:04X} colors={:08X},{:08X},{:08X},{:08X} "
                    "(guest normalized state before target update; not a visibility proof)",
                    count,fsi?"fsi":"fbo",surface,depth,control,mask,colors[0],colors[1],colors[2],colors[3]);
}
extern "C" void RexFnvGraphicsReport(FILE* file, double frames) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    const auto l=legacy.exchange(0),d=dynamic.exchange(0),b=barriers.exchange(0),t=transitions.exchange(0);
    std::fprintf(file,"     FNV render topology: legacy=%llu dynamic=%llu barriers=%llu broad=%llu "
                      "memory_items=%llu buffer_items=%llu image_items=%llu state_updates=%llu "
                      "target_state_changes=%llu fsi=%llu fbo=%llu | passes/frame=%.2f barriers/frame=%.2f "
                      "changes/frame=%.2f (deferred guest commands; host presenter excluded)\n",
                 (unsigned long long)l,(unsigned long long)d,(unsigned long long)b,(unsigned long long)broad.exchange(0),
                 (unsigned long long)memory_items.exchange(0),(unsigned long long)buffer_items.exchange(0),
                 (unsigned long long)image_items.exchange(0),(unsigned long long)states.exchange(0),
                 (unsigned long long)t,(unsigned long long)fsi_states.exchange(0),(unsigned long long)fbo_states.exchange(0),
                 frames>0?(l+d)/frames:0,frames>0?b/frames:0,frames>0?t/frames:0);
    std::fprintf(file,"     FNV depth exports: fbo_float24=%llu fbo_fixed24=%llu fsi=%llu half_scale_fix=%llu\n",
                 (unsigned long long)depth_export_fbo_float.exchange(0),
                 (unsigned long long)depth_export_fbo_fixed.exchange(0),
                 (unsigned long long)depth_export_fsi.exchange(0),
                 (unsigned long long)depth_export_remap.exchange(0));
}
extern "C" void RexFnvGraphicsImage(const void* input, bool depth, uint32_t format, uint32_t base) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    const auto& info=*static_cast<const VkImageCreateInfo*>(input);
    static unsigned count=0;
    if (count>=48) return;
    REXLOG_INFO("FNV image snapshot {}: depth={} guest_format={} base_tile={} host_format={} size={}x{} "
                "samples={} usage={:X} flags={:X} (requested image creation metadata)",
                ++count,depth,format,base,uint32_t(info.format),info.extent.width,info.extent.height,
                uint32_t(info.samples),info.usage,info.flags);
}
extern "C" void RexFnvGraphicsResolve(bool fsi, uint32_t dest, uint32_t length,
                                     uint32_t base, uint32_t used, uint32_t rows, uint32_t pitch) {
    if (!RexFnvGraphicsTraceEnabled()) return;
    static std::array<std::array<uint64_t,7>,48> seen{};
    static unsigned count=0;
    const std::array<uint64_t,7> key={uint64_t(fsi),dest,length,base,used,rows,pitch};
    if (First(seen,count,key))
        REXLOG_INFO("FNV resolve snapshot {}: effective={} dest={:08X} length={} base_tile={} "
                    "used_tiles={} rows={} pitch_tiles={} (requested copy; no readback added)",
                    count,fsi?"fsi":"fbo",dest,length,base,used,rows,pitch);
}
