// fnv_xbox - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
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
