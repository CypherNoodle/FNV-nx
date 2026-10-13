// CPU-side texture snapshots for native RHI development, not GPU readback.
#if REX_PLATFORM_SWITCH
void FnvCaptureTextures(const RegisterFile& regs, uint32_t mask,
                        const rex::memory::Memory* memory,
                        const std::filesystem::path& folder, const char* stem) {
  static uint64_t written = 0;
  if (!REXCVAR_GET(fnv_capture_textures)) return;
  const auto directory = folder / (std::string(stem) + "-textures");
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (error) { REXGPU_ERROR("FNV texture capture: cannot create directory"); return; }
  const auto marker = directory / "capture.json";
  std::filesystem::remove(marker, error);
  if (error) return;
  std::string entries;
  uint64_t draw_bytes = 0;
  unsigned complete = 0, skipped = 0;
  auto dump = [&](const std::filesystem::path& path, uint32_t address, uint32_t bytes) -> int {
    if (!bytes) return 0;
    if (uint64_t(address) + bytes > 0x20000000 || bytes > 8 * 1024 * 1024 ||
        draw_bytes + bytes > 16 * 1024 * 1024 || written + bytes > 64 * 1024 * 1024) return -2;
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return -3;
    bool ok = true;
    uint32_t offset = 0;
    while (offset < bytes) {
      const uint32_t current = address + offset;
      const uint32_t count = std::min(bytes - offset, 4096u - (current & 4095u));
      const uint64_t window = reinterpret_cast<uint64_t>(memory->physical_membase()) + current;
      auto* first = static_cast<const uint8_t*>(RexGmShadowFor(window));
      auto* last = static_cast<const uint8_t*>(RexGmShadowFor(window + count - 1));
      if (!first || reinterpret_cast<uintptr_t>(last) != reinterpret_cast<uintptr_t>(first) + count - 1 ||
          std::fwrite(first, 1, count, file) != count) { ok = false; break; }
      offset += count;
    }
    if (std::fclose(file) != 0) ok = false;
    // Charge successful writes even on partial failure, keeping the IO budget bounded.
    written += offset; draw_bytes += offset;
    if (!ok) { std::filesystem::remove(path, error); return -1; }
    return 1;
  };
  for (uint32_t slot = 0; slot < 32; ++slot) {
    if (!(mask & (uint32_t(1) << slot))) continue;
    const auto fetch = regs.GetTextureFetch(slot);
    if (fetch.type != xenos::FetchConstantType::kTexture) { ++skipped; continue; }
    uint32_t width, height, depth, base, mips, min_level, max_level;
    texture_util::GetSubresourcesFromFetchConstant(fetch, &width, &height, &depth,
                                                   &base, &mips, &min_level, &max_level);
    const auto format = GetBaseFormat(fetch.format);
    const auto* info = FormatInfo::Get(format);
    if (!info || !info->bytes_per_block() || max_level >= xenos::kTextureMaxMips) { ++skipped; continue; }
    const auto layout = texture_util::GetGuestTextureLayout(fetch.dimension, fetch.pitch,
        width + 1, height + 1, depth + 1, fetch.tiled, format, fetch.packed_mips, base != 0, max_level);
    const uint32_t base_bytes = base ? layout.base.level_data_extent_bytes : 0;
    const uint32_t mip_bytes = mips ? layout.mips_total_extent_bytes : 0;
    const std::string prefix = "fetch-" + std::to_string(slot);
    const int base_status = dump(directory / (prefix + ".base.bin"), base << 12, base_bytes);
    const int mip_status = dump(directory / (prefix + ".mips.bin"), mips << 12, mip_bytes);
    if ((!base_bytes || base_status == 1) && (!mip_bytes || mip_status == 1)) ++complete;
    else ++skipped;
    if (!entries.empty()) entries += ",";
    entries += fmt::format(
        "{{\"slot\":{},\"width\":{},\"height\":{},\"depth\":{},\"dimension\":{},"
        "\"format\":{},\"endian\":{},\"tiled\":{},\"pitch_texels\":{},\"packed_mips\":{},"
        "\"packed_level\":{},\"min_level\":{},\"max_level\":{},\"array_size\":{},"
        "\"block_width\":{},\"block_height\":{},\"bytes_per_block\":{},"
        "\"base_address\":{},\"base_bytes\":{},\"base_status\":{},"
        "\"mip_address\":{},\"mip_bytes\":{},\"mip_status\":{},"
        "\"base_row_pitch\":{},\"base_array_stride\":{},\"base_slice_rows\":{},"
        "\"fetch\":[{},{},{},{},{},{}],\"levels\":[",
        slot, width + 1, height + 1, depth + 1, uint32_t(fetch.dimension), uint32_t(format),
        uint32_t(fetch.endianness), bool(fetch.tiled), fetch.pitch * 32, bool(fetch.packed_mips),
        layout.packed_level, min_level, max_level, layout.array_size,
        info->block_width, info->block_height, info->bytes_per_block(), base << 12, base_bytes, base_status,
        mips << 12, mip_bytes, mip_status, layout.base.row_pitch_bytes,
        layout.base.array_slice_stride_bytes, layout.base.z_slice_stride_block_rows,
        fetch.dword_0, fetch.dword_1, fetch.dword_2, fetch.dword_3, fetch.dword_4, fetch.dword_5);
    const uint32_t first_stored_level = layout.packed_level == 0 ? 0 : 1;
    for (uint32_t level = first_stored_level; level <= layout.max_level; ++level) {
      const auto& mip = layout.mips[level];
      if (level != first_stored_level) entries += ",";
      entries += fmt::format("{{\"level\":{},\"offset\":{},\"row_pitch\":{},\"array_stride\":{},"
          "\"slice_rows\":{},\"bytes\":{}}}", level, layout.mip_offsets_bytes[level],
          mip.row_pitch_bytes, mip.array_slice_stride_bytes, mip.z_slice_stride_block_rows,
          mip.level_data_extent_bytes);
    }
    entries += "]}";
  }
  const std::string json = fmt::format("{{\"format\":1,\"texture_mask\":{},\"cpu_snapshot\":true,"
      "\"gpu_written_memory_coherence_verified\":false,\"complete\":{},\"skipped\":{},\"textures\":[{}]}}\n",
      mask, complete, skipped, entries);
  FILE* file = std::fopen(marker.string().c_str(), "wb");
  if (!file) { REXGPU_ERROR("FNV texture capture: cannot write metadata"); return; }
  bool ok = std::fwrite(json.data(), 1, json.size(), file) == json.size();
  if (std::fclose(file) != 0) ok = false;
  if (!ok) { std::filesystem::remove(marker, error); return; }
  REXGPU_INFO("FNV texture capture {}: {} complete, {} skipped, {} bytes; CPU snapshots", stem,
              complete, skipped, draw_bytes);
}
#endif
