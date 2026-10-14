#include "settings_ini.h"
#include "switch_settings.h"
#include <atomic>
#include <fstream>
#include <iterator>
#include <cstring>
#include <switch.h>
#include <rex/cvar.h>
#include <rex/logging.h>

namespace {
std::atomic<int> main_core{-1}, gpu_core{-1}, audio_core{-1};
}
void LoadFnvSwitchSettings() {
    const char* path = "/switch/FNV-nx/fnv-settings.ini";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { REXLOG_INFO("FNV INI absent: keeping compiled defaults; TOML is not read"); return; }
    const auto size = file.tellg();
    if (size < 0 || size > 16384) { REXLOG_WARN("FNV INI rejected: size must be at most 16 KiB"); return; }
    file.seekg(0);
    const std::string text(std::istreambuf_iterator<char>{file}, {});
    const auto settings = fnv::ParseSettingsIni(text);
    for (const auto& error : settings.errors) REXLOG_WARN("FNV INI ignored setting: {}", error);
    if (!settings.valid_version) { REXLOG_WARN("FNV INI rejected: unsupported version"); return; }
    for (const auto& [name, value] : settings.flags) {
        if (rex::cvar::SetFlagByName(name, value)) REXLOG_INFO("FNV INI applied: {}={}", name, value);
        else REXLOG_WARN("FNV INI could not apply: {}={}", name, value);
    }
    if (settings.main_core) main_core.store(*settings.main_core);
    if (settings.gpu_core) gpu_core.store(*settings.gpu_core);
    if (settings.audio_core) audio_core.store(*settings.audio_core);
    REXLOG_INFO("FNV INI CPU: main={} GPUCommands={} audio={} (-1=auto; no priority change)",
                main_core.load(), gpu_core.load(), audio_core.load());
}
extern "C" void RexFnvApplyThreadAffinity(uint32_t handle, const char* name) {
    if (!handle || !name) return;
    int core = -1;
    if (std::strncmp(name, "Main XThread", 12) == 0) core = main_core.load();
    else if (std::strncmp(name, "GPU Commands", 12) == 0) core = gpu_core.load();
    else if (std::strncmp(name, "Audio ", 6) == 0 || std::strncmp(name, "XMA ", 4) == 0)
        core = audio_core.load();
    if (core < 0) return;
    u64 allowed = 0, mask = 0;
    s32 preferred = -1;
    const u64 bit = 1ull << core;
    if (R_FAILED(svcGetInfo(&allowed, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0)) || !(allowed & bit)) return;
    if (R_FAILED(svcGetThreadCoreMask(&preferred, &mask, handle)) || mask == bit) return;
    const Result result = svcSetThreadCoreMask(handle, core, bit);
    REXLOG_INFO("FNV CPU affinity: thread={} requested_core={} old_mask={:X} result={:X}",
                name, core, mask, result);
}
