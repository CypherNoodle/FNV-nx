// fnv_xbox - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#if defined(__SWITCH__)
#include <cstdlib>
#endif
#if defined(_WIN32)
#include <thread>
void InstallFnvHostCrashObserver();
std::jthread StartFnvFrameWatch(rex::Runtime* runtime);
void InstallFnvTestPad(rex::Runtime* runtime);
#endif

class FnvXboxApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<FnvXboxApp>(new FnvXboxApp(ctx, "fnv_xbox",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
#if defined(__SWITCH__)
  std::string_view BuiltInConfig() const override {
    return R"(
log_to_file = true
log_level = "info"
log_file = "/switch/FNV-nx/logs/fnv-nx.log"
allow_game_relative_writes = false
asset_pack = false
asset_pack_build = false
input_xbox_layout = false
)";
  }
  void OnPostInitLogging() override {
    // The inline game folder also contains runtime files; never pack it.
    rex::cvar::SetFlagByName("asset_pack", "false");
    rex::cvar::SetFlagByName("asset_pack_build", "false");
    rex::cvar::SetFlagByName("allow_game_relative_writes", "false");
    // Make the driver cache location explicit before creating the GPU plugin.
    std::error_code cache_error;
    const char* cache = "/switch/FNV-nx/cache/mesa";
    std::filesystem::create_directories(cache, cache_error);
    if (!cache_error) {
      setenv("MESA_SHADER_CACHE_DIR", cache, 1);
      setenv("MESA_SHADER_CACHE_SHOW_STATS", "true", 0);
      REXLOG_INFO("FNV Mesa disk cache directory: {}", cache);
    } else {
      REXLOG_WARN("FNV Mesa cache directory unavailable: {}", cache_error.message());
    }
  }
#endif
  void OnConfigurePaths(rex::PathConfig& paths) override {
#if defined(__SWITCH__)
    // libstdc++ treats "sdmc:" as relative. libnx resolves this absolute
    // POSIX path on its default SD device, i.e. sdmc:/switch/FNV-nx.
    const std::filesystem::path root{"/switch/FNV-nx"};
    paths.game_data_root = root;
    paths.user_data_root = root / "user-data";
    paths.update_data_root = root / "updates";
    paths.cache_root = root / "cache";
    paths.metadata_root = root / "metadata";
    paths.config_path = root / "fnv-nx.toml";
#endif
  }
  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
  }
  void OnPostSetup() override {
    // This build opens archive basenames without a device prefix. Map only
    // existing top-level Data archives to their real read-only VFS paths.
    // Missing optional files still fail; this is not general cwd emulation.
    auto data = runtime()->game_data_root() / "Data";
    std::error_code error;
    unsigned count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(data, error)) {
      if (!entry.is_regular_file()) continue;
      auto extension = entry.path().extension().string();
      if (extension != ".bsa" && extension != ".esm") continue;
      auto name = entry.path().filename().string();
      runtime()->file_system()->RegisterSymbolicLink(name, "game:\\Data\\" + name);
      ++count;
    }
    REXLOG_INFO("FNV existing Data archive aliases: {}", count);
#if defined(_WIN32)
    InstallFnvTestPad(runtime());
#endif
  }
  void OnPreLaunchModule() override {
#if defined(_WIN32)
    InstallFnvHostCrashObserver();
    frame_watch_=StartFnvFrameWatch(runtime());
#endif
  }
  void OnShutdown() override {
#if defined(_WIN32)
    frame_watch_.request_stop();
    if(frame_watch_.joinable()) frame_watch_.join();
#endif
  }
#if defined(_WIN32)
 private:
  std::jthread frame_watch_;
#endif
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
