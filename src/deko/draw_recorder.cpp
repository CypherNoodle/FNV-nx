#include "draw_recorder.h"

namespace fnv::deko {
namespace {
void Bind(DkCmdBuf commands, DkStage stage, const StageResources& resources) {
  for (const auto& binding : resources.uniforms)
    dkCmdBufBindUniformBuffers(commands, stage, binding.slot, &binding.buffer, 1);
  for (const auto& binding : resources.storage)
    dkCmdBufBindStorageBuffers(commands, stage, binding.slot, &binding.buffer, 1);
  for (const auto& binding : resources.textures)
    dkCmdBufBindTextures(commands, stage, binding.slot, &binding.texture, 1);
}
}
bool RecordNativeDraw(DkCmdBuf commands, const NativeDraw& draw) {
  if (!commands || !draw.vertex || !draw.fragment || !draw.vertex->Get() ||
      !draw.fragment->Get() || !draw.rasterizer || !draw.depth_stencil || !draw.color ||
      draw.color_targets.empty() || draw.color_targets.size() > 4 ||
      draw.blend.size() != draw.color_targets.size() || !draw.vertices || !draw.instances)
    return false;
  for (auto* image : draw.color_targets) if (!image) return false;
  if (draw.indices) {
    const auto& indices = *draw.indices;
    const uint32_t size = indices.format == DkIdxFormat_Uint16 ? 2 :
                          indices.format == DkIdxFormat_Uint32 ? 4 : 0;
    if (!size || !indices.buffer.addr || indices.buffer.addr % size ||
        (uint64_t(indices.first_index) + draw.vertices) * size > indices.buffer.size)
      return false;
  }
  dkCmdBufBindRenderTargets(commands, draw.color_targets.data(), draw.color_targets.size(), draw.depth_target);
  dkCmdBufBindRasterizerState(commands, draw.rasterizer);
  dkCmdBufBindDepthStencilState(commands, draw.depth_stencil);
  dkCmdBufBindColorState(commands, draw.color);
  dkCmdBufBindBlendStates(commands, 0, draw.blend.data(), draw.blend.size());
  dkCmdBufSetViewports(commands, 0, &draw.viewport, 1);
  dkCmdBufSetScissors(commands, 0, &draw.scissor, 1);
  const DkShader* shaders[] = {draw.vertex->Get(), draw.fragment->Get()};
  dkCmdBufBindShaders(commands, DkStageFlag_Vertex | DkStageFlag_Fragment, shaders, 2);
  Bind(commands, DkStage_Vertex, draw.vertex_resources);
  Bind(commands, DkStage_Fragment, draw.fragment_resources);
  if (draw.indices) {
    const auto& indices = *draw.indices;
    dkCmdBufBindIdxBuffer(commands, indices.format, indices.buffer.addr);
    dkCmdBufSetPrimitiveRestart(commands, indices.restart, indices.restart_index);
    dkCmdBufDrawIndexed(commands, draw.primitive, draw.vertices, draw.instances,
                       indices.first_index, indices.vertex_offset, draw.first_instance);
  } else {
    dkCmdBufSetPrimitiveRestart(commands, false, 0);
    dkCmdBufDraw(commands, draw.primitive, draw.vertices, draw.instances, draw.first_vertex, draw.first_instance);
  }
  return true;
}
}
