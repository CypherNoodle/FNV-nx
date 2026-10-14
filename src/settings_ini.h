#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fnv {
struct IniSettings {
    std::vector<std::pair<std::string, std::string>> flags;
    std::vector<std::string> errors;
    std::optional<int> main_core, gpu_core, audio_core;
    bool valid_version = true;
};
inline std::string_view Trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == s.npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}
inline bool IniInteger(std::string_view value, int& number) {
    const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
inline IniSettings ParseSettingsIni(std::string_view text) {
    IniSettings out;
    std::string section;
    std::vector<std::string> keys;
    unsigned line = 0;
    while (!text.empty()) {
        ++line;
        const auto end = text.find('\n');
        auto row = Trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (line == 1 && row.starts_with("\xEF\xBB\xBF")) row.remove_prefix(3);
        row = Trim(row.substr(0, row.find_first_of(";#")));
        if (row.empty()) continue;
        auto error = [&] { out.errors.push_back("linea " + std::to_string(line) + ": " + std::string(row)); };
        if (row.front() == '[' && row.back() == ']') {
            section = std::string(Trim(row.substr(1, row.size() - 2)));
            continue;
        }
        const auto equals = row.find('=');
        if (equals == row.npos) { error(); continue; }
        const auto key = Trim(row.substr(0, equals)), value = Trim(row.substr(equals + 1));
        const std::string full = section + "." + std::string(key);
        bool duplicate = false;
        for (const auto& prior : keys) duplicate |= prior == full;
        if (duplicate) { error(); continue; }
        keys.push_back(full);
        const auto flag = [&](const char* name, std::string_view v) { out.flags.emplace_back(name, v); };
        if (full == ".version") {
            out.valid_version = value == "1";
            if (!out.valid_version) error();
        } else if (full == "graficos.resolucion") {
            if (value == "960x540" || value == "1280x720") {
                flag("fnv_fixed_render_mode", value == "960x540" ? "true" : "false");
                flag("fnv_render_width", value == "960x540" ? "960" : "1280");
                flag("fnv_render_height", value == "960x540" ? "540" : "720");
            } else error();
        } else if (full == "graficos.vsync") {
            // Presentation only. Keep the emulated Xbox vblank/timebase unchanged.
            if (value == "auto" || value == "off" || value == "on" || value == "relaxed") {
                flag("vulkan_allow_present_mode_immediate", value == "auto" || value == "off" ? "true" : "false");
                flag("vulkan_allow_present_mode_mailbox", value == "auto" ? "true" : "false");
                flag("vulkan_allow_present_mode_fifo_relaxed", value == "auto" || value == "relaxed" ? "true" : "false");
            } else error();
        } else if (full == "graficos.renderer") {
            if (value == "fsi" || value == "fbo") flag("render_target_path_vulkan", value);
            else error();
        } else if (section == "cpu" && (key == "principal" || key == "comandos_gpu" || key == "audio")) {
            int core = -1;
            if (value != "auto" && (!IniInteger(value, core) || core < 0 || core > 2)) { error(); continue; }
            if (key == "principal") out.main_core = core;
            if (key == "comandos_gpu") out.gpu_core = core;
            if (key == "audio") out.audio_core = core;
        } else {
            const char* name = nullptr;
            if (full == "optimizacion.espera_gpu_notificada") name = "fnv_gpu_wait_notify";
            if (full == "optimizacion.backoff_gpu") name = "fnv_gpu_wait_backoff";
            if (full == "optimizacion.agrupar_envios_gpu") name = "fnv_batch_submissions";
            if (full == "pantalla.mostrar_fps") name = "fnv_show_fps";
            if (full == "diagnostico.tiempos_cpu") name = "fnv_call_profile";
            if (full == "diagnostico.tiempos_gpu") name = "fnv_gpu_timestamps";
            if (full == "diagnostico.traza_guardado") name = "fnv_save_trace";
            if (full == "diagnostico.stacks_cpu") name = "switch_profile_stacks";
            if (name && (value == "true" || value == "false")) flag(name, value);
            else error();
        }
    }
    return out;
}
}
