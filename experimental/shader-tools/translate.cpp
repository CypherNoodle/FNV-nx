#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/shader/spirv_translator.h>
#include <rex/string/buffer.h>
#include <rex/logging.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 6 && argc != 7) {
    std::fprintf(stderr, "Usage: fnv-shader-translate input output vert|frag ucode_hash_hex modification_hex [fsi|fsi-native]\n");
    return 2;
  }
  rex::InitLogging(nullptr, spdlog::level::err);
  std::ifstream input(argv[1], std::ios::binary | std::ios::ate);
  const auto length = input.tellg();
  if (!input || length <= 0 || length > 1024 * 1024 || length % 4) return 3;
  std::vector<uint32_t> words(size_t(length) / 4);
  input.seekg(0); input.read(reinterpret_cast<char*>(words.data()), length);
  if (!input) return 3;
  const bool vertex = std::string_view(argv[3]) == "vert";
  const bool native_demote = argc == 7 && std::string_view(argv[6]) == "fsi-native";
  const bool fsi = argc == 7 && (std::string_view(argv[6]) == "fsi" || native_demote);
  if (argc == 7 && !fsi) return 2;
  if (!vertex && std::string_view(argv[3]) != "frag") return 2;
  rex::graphics::Shader shader(vertex ? rex::graphics::xenos::ShaderType::kVertex :
                                      rex::graphics::xenos::ShaderType::kPixel,
      std::strtoull(argv[4], nullptr, 16), words.data(), words.size(), std::endian::little);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);
  rex::graphics::SpirvShaderTranslator::Features features(true);
  // Use the SDK's deferred pixel-kill implementation. It preserves guest
  // derivatives before terminating; do not rewrite SPIR-V demote to discard.
  features.demote_to_helper_invocation = native_demote;
  features.fragment_shader_sample_interlock = false;
  const uint64_t modification = std::strtoull(argv[5], nullptr, 16);
  if (!vertex && rex::graphics::SpirvShaderTranslator::Modification(modification).pixel.depth_stencil_mode ==
                     rex::graphics::SpirvShaderTranslator::Modification::DepthStencilMode::kFsiLateOpaque &&
      (!fsi || !shader.implicit_early_z_write_allowed())) {
    std::fprintf(stderr, "Late FSI variant rejected: requires opaque shader without kill/depth/memexport\n");
    return 6;
  }
  rex::graphics::SpirvShaderTranslator translator(features, true, true, fsi, 1, 1);
  auto* translation = shader.GetOrCreateTranslation(modification);
  if (!translator.TranslateAnalyzedShader(*translation) || !translation->is_valid()) return 4;
  const auto& binary = translation->translated_binary();
  std::ofstream output(argv[2], std::ios::binary);
  output.write(reinterpret_cast<const char*>(binary.data()), binary.size());
  if (!output) return 5;
  std::printf("Translated %zu bytes; backend=%s kills=%u depth=%u memexport=%u opaque=%u; GPU validation pending\n",
              binary.size(), fsi ? "fsi" : "host", unsigned(shader.kills_pixels()),
              unsigned(shader.writes_depth()), unsigned(bool(shader.memexport_eM_written())),
              unsigned(shader.implicit_early_z_write_allowed()));
  return 0;
}
