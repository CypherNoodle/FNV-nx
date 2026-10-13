// Included inside the pinned Vulkan command processor's anonymous namespace.
// Private CPU-side geometry snapshots; textures/EDRAM and GPU writes are NOT captured.
#if REX_PLATFORM_SWITCH
bool FnvCaptureDraw(const RegisterFile& regs, const VulkanShader& vs,
                    const VulkanShader* ps, uint64_t vs_mod, uint64_t ps_mod,
                    const PrimitiveProcessor::ProcessingResult& draw,
                    const SpirvShaderTranslator::SystemConstants& constants,
                    const VkViewport& viewport, const VkRect2D& scissor,
                    const rex::memory::Memory* memory, bool memexport) {
  static std::vector<std::pair<uint64_t, uint64_t>> attempted;
  static unsigned completed = 0;
  static uint64_t total_bytes = 0;
  if (!REXCVAR_GET(fnv_capture_draws) || completed >= 8 || attempted.size() >= 16 ||
      !ps || memexport || draw.host_draw_vertex_count < 96 ||
      draw.host_vertex_shader_type != Shader::HostVertexShaderType::kVertex ||
      draw.index_buffer_type != PrimitiveProcessor::kGuestDMA ||
      REXCVAR_GET(dump_shaders).empty()) return false;
  const auto key = std::make_pair(vs.ucode_data_hash(), ps->ucode_data_hash());
  if (std::find(attempted.begin(), attempted.end(), key) != attempted.end()) return false;
  attempted.push_back(key);
  struct Range { uint32_t address, size, fetch; };
  std::vector<Range> ranges;
  uint64_t bytes = 0;
  const auto& map = vs.constant_register_map();
  for (uint32_t i = 0; i < rex::countof(map.vertex_fetch_bitmap); ++i) {
    uint32_t bits = map.vertex_fetch_bitmap[i], bit;
    while (rex::bit_scan_forward(bits, &bit)) {
      bits &= ~(uint32_t(1) << bit);
      const uint32_t slot = i * 32 + bit;
      const auto fetch = regs.GetVertexFetch(slot);
      const uint64_t address = uint64_t(fetch.address) * 4;
      const uint64_t size = uint64_t(fetch.size) * 4;
      if (fetch.type != xenos::FetchConstantType::kVertex || !size ||
          address + size > 0x20000000 || size > 8 * 1024 * 1024) return false;
      ranges.push_back({uint32_t(address), uint32_t(size), slot});
      bytes += size;
    }
  }
  const uint64_t index_size = uint64_t(draw.host_draw_vertex_count) *
      (draw.host_index_format == xenos::IndexFormat::kInt16 ? 2 : 4);
  if (ranges.empty() || uint64_t(draw.guest_index_base) + index_size > 0x20000000 ||
      index_size > 8 * 1024 * 1024 || bytes + index_size > 16 * 1024 * 1024 ||
      total_bytes + bytes + index_size > 64 * 1024 * 1024) return false;
  ranges.push_back({draw.guest_index_base, uint32_t(index_size), UINT32_MAX});
  const auto folder = std::filesystem::path(REXCVAR_GET(dump_shaders)) / "draw-captures";
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) { REXGPU_ERROR("FNV draw capture: cannot create directory"); return false; }
  char stem[64];
  std::snprintf(stem, sizeof(stem), "%02u-%016llx-%016llx", completed,
                (unsigned long long)key.first, (unsigned long long)key.second);
  auto binary = folder / (std::string(stem) + ".bin");
  auto manifest = folder / (std::string(stem) + ".json");
  std::filesystem::remove(manifest, error);
  if (error) return false;
  FILE* file = std::fopen(binary.string().c_str(), "wb");
  if (!file) { REXGPU_ERROR("FNV draw capture: cannot open output"); return false; }
  bool ok = true;
  auto write = [&](const void* source, size_t size) {
    if (ok && std::fwrite(source, 1, size, file) != size) ok = false;
  };
  // Little-endian container: magic/version; then [kind, address/slot, byte size] records.
  const uint32_t header[] = {0x44564E46, 1}; // FNVD
  write(header, sizeof(header));
  auto record = [&](uint32_t kind, uint32_t address, const void* data, uint32_t size) {
    const uint32_t tag[] = {kind, address, size};
    write(tag, sizeof(tag)); write(data, size);
  };
  record(1, 0, regs.values, sizeof(regs.values));
  record(2, 0, &constants, sizeof(constants));
  record(3, 0, &viewport, sizeof(viewport));
  record(4, 0, &scissor, sizeof(scissor));
  record(5, 0, map.float_bitmap, sizeof(map.float_bitmap));
  record(5, 1, ps->constant_register_map().float_bitmap,
         sizeof(ps->constant_register_map().float_bitmap));
  for (const auto& range : ranges) {
    const uint32_t tag[] = {range.fetch == UINT32_MAX ? 7u : 6u, range.address, range.size};
    write(tag, sizeof(tag));
    // Each page is resolved separately: never dereference a watched guest alias,
    // commit memory, or assume adjacent shadow chunks are contiguous.
    uint32_t offset = 0;
    while (ok && offset < range.size) {
      const uint32_t address = range.address + offset;
      const uint32_t count = std::min(range.size - offset, 4096u - (address & 4095u));
      const uint64_t window = reinterpret_cast<uint64_t>(memory->physical_membase()) + address;
      auto* first = static_cast<const uint8_t*>(RexGmShadowFor(window));
      auto* last = static_cast<const uint8_t*>(RexGmShadowFor(window + count - 1));
      if (!first || reinterpret_cast<uintptr_t>(last) !=
                        reinterpret_cast<uintptr_t>(first) + count - 1) { ok = false; break; }
      write(first, count); offset += count;
    }
  }
  if (std::fclose(file) != 0) ok = false;
  // JSON is written last and serves as the completion marker.
  if (ok) {
    file = std::fopen(manifest.string().c_str(), "wb");
    if (!file) ok = false;
    else {
      const int result = std::fprintf(file,
          "{\"format\":1,\"vertex_hash\":\"%016llx\",\"pixel_hash\":\"%016llx\","
          "\"vertex_modification\":\"%016llx\",\"pixel_modification\":\"%016llx\","
          "\"primitive\":%u,\"count\":%u,\"index_format\":%u,\"index_endian\":%u,"
          "\"index_base\":%u,\"primitive_restart\":%s,\"textures_captured\":false,"
          "\"gpu_written_memory_coherence_verified\":false}\n",
          (unsigned long long)key.first, (unsigned long long)key.second,
          (unsigned long long)vs_mod, (unsigned long long)ps_mod,
          unsigned(draw.host_primitive_type), draw.host_draw_vertex_count,
          unsigned(draw.host_index_format), unsigned(draw.host_shader_index_endian),
          draw.guest_index_base, draw.host_primitive_reset_enabled ? "true" : "false");
      if (std::fclose(file) != 0 || result < 0) ok = false;
    }
  }
  if (!ok) {
    std::filesystem::remove(binary, error); std::filesystem::remove(manifest, error);
    REXGPU_ERROR("FNV draw capture: incomplete snapshot discarded"); return false;
  }
  ++completed; total_bytes += bytes + index_size;
  REXGPU_INFO("FNV geometry capture {}: {} indices, {} bytes; no textures", stem,
              draw.host_draw_vertex_count, bytes + index_size);
  return true;
}
#endif
