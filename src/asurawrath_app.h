#pragma once

#include <rex/cvar.h>
#include <rex/rex_app.h>

#include "compat/rexglue_filesystem_bridge.h"
#include "platform/theseus_platform.h"

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>
#include <android/log.h>
#endif

// Global handle referenced by JNI
#if defined(__ANDROID__)
extern SDL_JoystickID g_VirtualJoystickID;
extern SDL_Gamepad *g_VirtualGamepad;
#endif

class AsurawrathApp : public rex::ReXApp {
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx) {
    return std::unique_ptr<AsurawrathApp>(
        new AsurawrathApp(ctx, "asura_wrath_recomp", PPCImageConfig));
  }

#if defined(__ANDROID__)
  void SetupVirtualGamepad() {
    if (g_VirtualJoystickID != 0)
      return;

    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
      SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 4;
    desc.nbuttons = 15;
    desc.name = "Virtual Touch Gamepad";

    g_VirtualJoystickID = SDL_AttachVirtualJoystick(&desc);

    if (g_VirtualJoystickID != 0) {
      SDL_GUID guid = SDL_GetJoystickGUIDForID(g_VirtualJoystickID);
      char guidStr[64];
      SDL_GUIDToString(guid, guidStr, sizeof(guidStr));

      char mapping[512];
      SDL_snprintf(
          mapping, sizeof(mapping),
          "%s,Virtual Touch "
          "Gamepad,a:b0,b:b1,x:b2,y:b3,back:b7,start:b6,leftshoulder:b4,"
          "rightshoulder:b5,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:"
          "a0,lefty:a1,rightx:a2,righty:a3,platform:Android,",
          guidStr);

      int res = SDL_AddGamepadMapping(mapping);
      g_VirtualGamepad = SDL_OpenGamepad(g_VirtualJoystickID);

      __android_log_print(
          ANDROID_LOG_INFO, "AsuraInput",
          "Native Virtual Gamepad Created (ID: %d, Mapping res: %d)",
          (int)g_VirtualJoystickID, res);
    } else {
      __android_log_print(ANDROID_LOG_ERROR, "AsuraInput",
                          "Failed to register Virtual Gamepad: %s",
                          SDL_GetError());
    }
  }
#endif

  void OnPreSetup(rex::RuntimeConfig &config) override {
    // T03: Theseus discovers the process location itself. Host filesystem
    // bootstrap no longer depends on ReXGlue.
    theseus::Platform::Instance().BootstrapFromProcess();
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
#endif
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  void SetupPcDataLayoutAliases() {
    auto& host = theseus::Platform::Instance();
    if (!host.initialized()) {
      host.BootstrapFromProcess();
    }

    // The physical PC filesystem belongs to Theseus. ReXGlue is retained here
    // only as a temporary guest-path/ABI bridge for Xbox-shaped filenames.
    asura::compat::MountPcDataLayout(
        runtime(), game_data_root(), user_data_root(),
        host.paths(), host.files());
  }

  void OnPostSetup() override {
    SetupPcDataLayoutAliases();
#if defined(__ANDROID__)
    SetupVirtualGamepad();
#endif
  }

  void OnConfigurePaths(rex::PathConfig &paths) override {
    std::error_code ec;

#if defined(_WIN32) && !defined(__ANDROID__)
    // Portable Windows layout by default. An explicit --user_data_root still
    // wins, which keeps the normal ReXGlue override available for power users.
    if (REXCVAR_GET(user_data_root).empty()) {
      auto &host = theseus::Platform::Instance();
      if (!host.initialized()) {
        host.BootstrapFromProcess();
      }
      paths.user_data_root = host.paths().user_data;
    }
#endif

    auto &host = theseus::Platform::Instance();
    if (!host.initialized()) {
      host.BootstrapFromProcess();
    }
    auto &files = host.files();

    files.CreateDirectories(paths.user_data_root);
    auto cache_dir = paths.user_data_root / "cache";
    if (paths.user_data_root == host.paths().user_data) {
      cache_dir = host.paths().cache;
    }
    files.CreateDirectories(cache_dir);
    paths.cache_root = cache_dir;

    auto cwd = std::filesystem::current_path(ec);
    if (auto resolved =
            host.ResolveGameDataRoot(paths.game_data_root, cwd)) {
      paths.game_data_root = *resolved;
    } else if (paths.game_data_root.empty()) {
      // Keep a deterministic portable fallback. A missing default.xex will
      // still be reported by the existing runtime validation, but path
      // ownership no longer falls back to Documents/AppData.
      paths.game_data_root = host.paths().data;
    }

    if (files.IsFile(paths.game_data_root)) {
      auto ext = paths.game_data_root.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".iso" || ext == ".gdfx") {
        auto iso_path = paths.game_data_root;
        auto dest_dir = paths.user_data_root.empty()
                            ? iso_path.parent_path() / "extracted"
                            : paths.user_data_root / "extracted";
        if (!files.IsFile(dest_dir / "default.xex") &&
            !files.IsFile(dest_dir / "BCGame" / "default.xex")) {
          if (asura::compat::ExtractDiscImageToDirectory(
                  iso_path, dest_dir, files)) {
            files.RemoveFile(iso_path);
            files.RemoveFile(paths.user_data_root / "Asura's Wrath.iso");
            files.RemoveFile(dest_dir / "Asura's Wrath.iso");
            if (auto resolved_after_extract =
                    host.ResolveGameDataRoot(dest_dir, dest_dir)) {
              paths.game_data_root = *resolved_after_extract;
            } else {
              paths.game_data_root = dest_dir;
            }
          }
        } else {
          files.RemoveFile(iso_path);
          files.RemoveFile(paths.user_data_root / "Asura's Wrath.iso");
          files.RemoveFile(dest_dir / "Asura's Wrath.iso");
          if (auto resolved_after_extract =
                  host.ResolveGameDataRoot(dest_dir, dest_dir)) {
            paths.game_data_root = *resolved_after_extract;
          } else {
            paths.game_data_root = dest_dir;
          }
        }
      }
    }
  }
};
