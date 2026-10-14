"""Install bounded GPU measurement, optional batching, FPS and explicit affinity."""
import argparse
from pathlib import Path
import re

BEGIN = '// FNV alpha12 extension begin'
END = '// FNV alpha12 extension end'


def remove_extensions(text):
    return re.sub(r'^([ \t]*)' + BEGIN + r'\n.*?^\1' + END + r'\n',
                  '', text, flags=re.MULTILINE | re.DOTALL)


def prepare(sdk):
    original = {}
    texts = {}
    def read(name):
        original[name] = remove_extensions((sdk / name).read_text())
        texts[name] = original[name]
    def insert(name, anchor, code, before=False, indent=''):
        text = texts[name]
        assert text.count(anchor) == 1, (name, anchor)
        block = indent + BEGIN + '\n' + code.rstrip() + '\n' + indent + END + '\n'
        texts[name] = text.replace(anchor, block + anchor if before else anchor + block, 1)

    cp = 'src/graphics/vulkan/command_processor.cpp'
    h = 'include/rex/graphics/vulkan/command_processor.h'
    deferred = 'include/rex/graphics/vulkan/deferred_command_buffer.h'
    execute = 'src/graphics/vulkan/deferred_command_buffer.cpp'
    presenter = 'src/ui/vulkan/vulkan_presenter.cpp'
    perf = 'src/ui/switch_perf.cpp'
    for name in (cp, h, deferred, execute, presenter, perf): read(name)
    insert(h, 'namespace rex::graphics::vulkan {\n',
           '#if REX_PLATFORM_SWITCH\n#include <rex/graphics/vulkan/fnv_gpu_timestamps.h>\n#endif', before=True)
    insert(h, '  void OnPrimaryBufferEnd() override;\n',
           '  #if REX_PLATFORM_SWITCH\n  void PrepareForWait() override;\n  fnv::GpuTimestampProbe fnv_gpu_probe_;\n  #endif', indent='  ')
    insert(cp, '#if REX_PLATFORM_SWITCH\n// Optional application extension',
           '#if REX_PLATFORM_SWITCH\nextern "C" bool RexFnvBatchSubmissionsEnabled();\nextern "C" void RexFnvFpsFrame();\nextern "C" void RexFnvDepthState(bool, bool, bool, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);\n#endif', before=True)
    insert(cp, 'void VulkanCommandProcessor::OnPrimaryBufferEnd() {\n',
           '  #if REX_PLATFORM_SWITCH\n  if (RexFnvBatchSubmissionsEnabled()) return;\n  #endif', indent='  ')
    insert(cp, 'void VulkanCommandProcessor::OnPrimaryBufferEnd() {\n', '''
#if REX_PLATFORM_SWITCH
void VulkanCommandProcessor::PrepareForWait() {
  // Flush pending work before idle or WAIT_REG_MEM; never hold GPU commands
  // behind a producer/consumer wait. EndSubmission keeps all original fences.
  if (RexFnvBatchSubmissionsEnabled() && submission_open_) EndSubmission(false);
  CommandProcessor::PrepareForWait();
}
#endif''', before=True)
    insert(cp, '    deferred_command_buffer_.Reset();\n',
           '    #if REX_PLATFORM_SWITCH\n    fnv_gpu_probe_.Begin(GetVulkanDevice(), deferred_command_buffer_, submission_completed_);\n    #endif',
           indent='    ')
    insert(cp, '    uint64_t submission_current = GetCurrentSubmission();\n',
           '    #if REX_PLATFORM_SWITCH\n    fnv_gpu_probe_.Commit(submission_current);\n    #endif', indent='    ')
    insert(cp, '    deferred_command_buffer_.Execute(command_buffer.buffer);\n',
           '    #if REX_PLATFORM_SWITCH\n    fnv_gpu_probe_.Finish(deferred_command_buffer_);\n    #endif', before=True, indent='    ')
    insert(cp, 'void VulkanCommandProcessor::ShutdownContext() {\n  AwaitAllQueueOperationsCompletion();\n',
           '  #if REX_PLATFORM_SWITCH\n  fnv_gpu_probe_.Shutdown();\n  #endif', indent='  ')
    insert(cp, '  RexSwitchPerfCount(0);\n', '  RexFnvFpsFrame();', indent='  ')
    insert(cp, '      return draw_fail("begin_submission");\n    }\n',
           '    #if REX_PLATFORM_SWITCH\n    fnv_gpu_probe_.Category(deferred_command_buffer_, 1);\n    #endif', indent='    ')
    insert(cp, '  ReadbackResolveMode readback_mode = GetReadbackResolveMode(REXCVAR_GET(vulkan_readback_resolve));\n  if (readback_mode == ReadbackResolveMode::kDisabled) {\n    uint32_t written_address, written_length;\n',
           '  #if REX_PLATFORM_SWITCH\n  fnv_gpu_probe_.Category(deferred_command_buffer_, 2);\n  #endif', before=True, indent='  ')
    # Category 0 for the guest output postprocess, not another gameplay draw.
    insert(cp, '        auto& vulkan_context =\n',
           '        #if REX_PLATFORM_SWITCH\n        fnv_gpu_probe_.Category(deferred_command_buffer_, 0);\n        #endif', before=True, indent='        ')
    insert(cp, '  bool dirty = false;\n\n  // Flags.\n', '''
  #if REX_PLATFORM_SWITCH
  if (edram_fragment_shader_interlock) {
    RexFnvDepthState(normalized_depth_control.z_enable || normalized_depth_control.stencil_enable,
                     depth_stencil_enabled, normalized_depth_control.z_write_enable,
                     rb_depth_info.depth_base, color_infos[0].color_base,
                     rb_surface_info.surface_pitch, uint32_t(normalized_depth_control.zfunc),
                     uint32_t(rb_depth_info.depth_format), normalized_color_mask);
  }
  #endif''', before=True, indent='  ')
    insert(deferred, '  void CmdVkResetQueryPool(VkQueryPool query_pool, uint32_t first_query, uint32_t query_count) {\n', '''
  #if REX_PLATFORM_SWITCH
  void CmdFnvWriteTimestamp(PFN_vkCmdWriteTimestamp write, VkQueryPool pool, uint32_t query) {
    auto& args = *reinterpret_cast<ArgsFnvWriteTimestamp*>(
        WriteCommand(Command::kFnvWriteTimestamp, sizeof(ArgsFnvWriteTimestamp)));
    args.write = write;
    args.pool = pool;
    args.query = query;
  }
  #endif''', before=True, indent='  ')
    insert(deferred, '    kVkResetQueryPool,\n',
           '    #if REX_PLATFORM_SWITCH\n    kFnvWriteTimestamp,\n    #endif', indent='    ')
    insert(deferred, '  struct ArgsVkResetQueryPool {\n', '''
  #if REX_PLATFORM_SWITCH
  struct ArgsFnvWriteTimestamp {
    PFN_vkCmdWriteTimestamp write;
    VkQueryPool pool;
    uint32_t query;
  };
  #endif''', before=True, indent='  ')
    insert(execute, '      case Command::kVkResetQueryPool: {\n', '''
      #if REX_PLATFORM_SWITCH
      case Command::kFnvWriteTimestamp: {
        auto& args = *reinterpret_cast<const ArgsFnvWriteTimestamp*>(stream);
        args.write(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, args.pool, args.query);
      } break;
      #endif''', before=True, indent='      ')
    insert(presenter, 'namespace rex {\n',
           '#if REX_PLATFORM_SWITCH\nextern "C" void RexFnvFpsDraw(VkCommandBuffer, uint32_t, uint32_t, PFN_vkCmdClearAttachments);\n#endif',
           before=True)
    # Exact anchor inside the final swapchain render pass, after UI if present.
    anchor = '    ExecuteUIDrawersFromUIThread(ui_draw_context);\n  }\n\n'
    insert(presenter, anchor, '''
  #if REX_PLATFORM_SWITCH
  RexFnvFpsDraw(draw_command_buffer, paint_context_.swapchain_extent.width,
                paint_context_.swapchain_extent.height, dfn.vkCmdClearAttachments);
  #endif''', indent='  ')
    insert(perf, 'extern "C" {\nvoid _start(void);\n',
           'void RexFnvApplyThreadAffinity(uint32_t handle, const char* name);')
    insert(perf, '    if (s.seen != h) {  // new thread: no interval yet\n',
           '    RexFnvApplyThreadAffinity(h, s.name);', before=True, indent='    ')
    insert(perf, '      std::memcpy(s.name, tmp, sizeof(tmp));\n',
           '      RexFnvApplyThreadAffinity(handle, name);', indent='      ')
    for name, text in texts.items():
        assert remove_extensions(text) == original[name], ('Original statements changed', name)
        path = sdk / name
        if path.read_text() != text: path.write_text(text)
    own = Path(__file__).resolve().parents[1]
    copies = {
        own / 'tools/horizon_gpu_timestamps.h': sdk / 'include/rex/graphics/vulkan/fnv_gpu_timestamps.h',
        own / 'src/gpu_timestamp_math.h': sdk / 'include/rex/graphics/vulkan/fnv_gpu_timestamp_math.h',
    }
    for source, destination in copies.items():
        if not destination.exists() or destination.read_bytes() != source.read_bytes():
            destination.write_bytes(source.read_bytes())


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    prepare(p.parse_args().sdk)
    print('PASS alpha12 SDK extension roundtrip: grouping/flush, sparse GPU timestamps, FPS, affinity')
