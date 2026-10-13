#pragma once
#include "shader_module.h"
#include <span>

namespace fnv::deko {
struct BufferBinding { uint32_t slot; DkBufExtents buffer; };
struct TextureBinding { uint32_t slot; DkResHandle texture; };
struct StageResources {
  std::span<const BufferBinding> uniforms;
  std::span<const BufferBinding> storage;
  std::span<const TextureBinding> textures;
};
struct IndexDraw {
  DkBufExtents buffer{};
  DkIdxFormat format = DkIdxFormat_Uint16;
  uint32_t first_index = 0;
  int32_t vertex_offset = 0;
  bool restart = false;
  uint32_t restart_index = 0xFFFF;
};
struct NativeDraw {
  const ShaderModule* vertex;
  const ShaderModule* fragment;
  std::span<const DkImageView* const> color_targets;
  const DkImageView* depth_target;
  const DkRasterizerState* rasterizer;
  const DkDepthStencilState* depth_stencil;
  const DkColorState* color;
  std::span<const DkBlendState> blend;
  DkViewport viewport;
  DkScissor scissor;
  StageResources vertex_resources, fragment_resources;
  DkPrimitive primitive;
  uint32_t vertices, instances, first_vertex, first_instance;
  // With indices, vertices is the index count. Data must be host endian.
  const IndexDraw* indices = nullptr;
};
// Resources, descriptor pools, barriers, command
// storage and GPU lifetime belong to the caller. No game/PM4 interception yet.
bool RecordNativeDraw(DkCmdBuf commands, const NativeDraw& draw);
}
