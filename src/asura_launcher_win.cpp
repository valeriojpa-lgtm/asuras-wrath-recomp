#if defined(_WIN32) && !defined(__ANDROID__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "asura_launcher_win.h"
#include "resource.h"
#include "platform/theseus_config.h"
#include "platform/theseus_platform.h"

#include <windows.h>
#include <commctrl.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace asura {
namespace {

constexpr wchar_t kLauncherClass[] = L"AsuraWrathNativeLauncher";
constexpr wchar_t kLauncherTitle[] = L"Asura's Wrath - Settings";

enum ControlId : int {
  kResolution = 1001,
  kDisplayMode,
  kRenderer,
  kAdapter,
  kVsync,
  kAsyncShaders,
  kMnk,
  kInputBackend,
  kLanguage,
  kShowAtStartup,
  kDefaults,
  kPlay,
  kExit,
};

struct ResolutionOption {
  int width;
  int height;
  const wchar_t* label;
};

constexpr ResolutionOption kResolutions[] = {
    {1280, 720, L"1280 x 720"},
    {1600, 900, L"1600 x 900"},
    {1920, 1080, L"1920 x 1080"},
    {2560, 1440, L"2560 x 1440"},
    {2560, 1600, L"2560 x 1600"},
    {3440, 1440, L"3440 x 1440"},
    {3840, 2160, L"3840 x 2160"},
};

struct LanguageOption {
  const wchar_t* label;
  int language_id;
  int country_id;
};

constexpr LanguageOption kLanguages[] = {
    {L"English", 1, 103},
    {L"Español", 5, 31},
    {L"Français", 4, 34},
    {L"Deutsch", 3, 24},
    {L"Italiano", 6, 50},
    {L"日本語", 2, 53},
};

struct AdapterOption {
  int ordinal = -1;
  std::wstring name;
  SIZE_T dedicated_video_memory = 0;
  bool software = false;
};

using LauncherSettings = theseus::Config;

std::filesystem::path ExecutableFolder() {
  std::wstring buffer(32768, L'\0');
  DWORD length =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) {
    return std::filesystem::current_path();
  }
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path ConfigPath() {
  auto& platform = theseus::Platform::Instance();
  platform.Bootstrap(ExecutableFolder());
  return platform.paths().config / "Asura.ini";
}

std::filesystem::path LegacyConfigPath() {
  return ExecutableFolder() / "UserData" / "Launcher.ini";
}

std::vector<AdapterOption> EnumerateAdapters() {
  std::vector<AdapterOption> result;
  IDXGIFactory1* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory) {
    return result;
  }

  for (UINT ordinal = 0;; ++ordinal) {
    IDXGIAdapter1* adapter = nullptr;
    HRESULT hr = factory->EnumAdapters1(ordinal, &adapter);
    if (hr == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    if (FAILED(hr) || !adapter) {
      continue;
    }

    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      AdapterOption option;
      option.ordinal = static_cast<int>(ordinal);
      option.name = desc.Description;
      option.dedicated_video_memory = desc.DedicatedVideoMemory;
      option.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
      result.push_back(std::move(option));
    }
    adapter->Release();
  }
  factory->Release();
  return result;
}

int BestAdapterOrdinal(const std::vector<AdapterOption>& adapters) {
  const AdapterOption* best = nullptr;
  for (const auto& adapter : adapters) {
    if (adapter.software) {
      continue;
    }
    if (!best || adapter.dedicated_video_memory > best->dedicated_video_memory) {
      best = &adapter;
    }
  }
  if (best) {
    return best->ordinal;
  }
  return adapters.empty() ? -1 : adapters.front().ordinal;
}

int DefaultLanguageIndex() {
  LANGID id = GetUserDefaultUILanguage();
  switch (PRIMARYLANGID(id)) {
    case LANG_SPANISH:
      return 1;
    case LANG_FRENCH:
      return 2;
    case LANG_GERMAN:
      return 3;
    case LANG_ITALIAN:
      return 4;
    case LANG_JAPANESE:
      return 5;
    default:
      return 0;
  }
}

LauncherSettings DefaultSettings() {
  LauncherSettings s;
  const int language_index = DefaultLanguageIndex();
  s.language = kLanguages[language_index].language_id;
  s.country = kLanguages[language_index].country_id;
  return s;
}

LauncherSettings LoadSettings(const std::filesystem::path& path) {
  LauncherSettings settings = DefaultSettings();
  (void)theseus::LoadConfig(path, settings);
  return settings;
}

void SaveSettings(const std::filesystem::path& path,
                  const LauncherSettings& settings) {
  (void)theseus::SaveConfig(path, settings);
}

bool HasArg(const std::vector<std::string>& args, std::string_view wanted) {
  return std::any_of(args.begin(), args.end(), [&](const std::string& arg) {
    return arg == wanted;
  });
}

void RemoveCustomLauncherArgs(std::vector<std::string>& args) {
  std::erase_if(args, [](const std::string& arg) {
    return arg == "--launcher" || arg == "--no_launcher";
  });
}

void RemoveManagedArgs(std::vector<std::string>& args) {
  static constexpr std::string_view prefixes[] = {
      "--window_width=", "--window_height=", "--fullscreen=",
      "--gpu_backend=", "--d3d12_adapter=", "--vsync=",
      "--async_shader_compilation=", "--mnk_mode=", "--mnk_mouse=",
      "--input_backend=", "--user_language=", "--user_country=",
  };
  std::erase_if(args, [](const std::string& arg) {
    for (const auto prefix : prefixes) {
      if (arg.starts_with(prefix)) {
        return true;
      }
    }
    return false;
  });
}

void AppendSettingsArgs(std::vector<std::string>& args,
                        const LauncherSettings& s,
                        const std::vector<AdapterOption>& adapters) {
  RemoveManagedArgs(args);

  auto add_int = [&](std::string_view name, int value) {
    args.emplace_back("--" + std::string(name) + "=" + std::to_string(value));
  };
  auto add_bool = [&](std::string_view name, bool value) {
    args.emplace_back("--" + std::string(name) + "=" + (value ? "true" : "false"));
  };
  auto add_string = [&](std::string_view name, std::string_view value) {
    args.emplace_back("--" + std::string(name) + "=" + std::string(value));
  };

  add_int("window_width", s.width);
  add_int("window_height", s.height);
  add_bool("fullscreen", s.fullscreen);
  add_string("gpu_backend", s.renderer == 1 ? "vulkan" : "d3d12");

  if (s.renderer == 0) {
    const int adapter =
        s.adapter >= 0 ? s.adapter : BestAdapterOrdinal(adapters);
    if (adapter >= 0) {
      add_int("d3d12_adapter", adapter);
    }
  }

  add_bool("vsync", s.vsync);
  add_bool("async_shader_compilation", s.async_shaders);
  add_bool("mnk_mode", s.mnk);
  add_bool("mnk_mouse", false);
  add_string("input_backend", s.input_backend == 1 ? "xinput" : "sdl");
  add_int("user_language", s.language);
  add_int("user_country", s.country);
}

HWND CreateLabel(HWND parent, HFONT font, const wchar_t* text,
                 int x, int y, int w, int h) {
  HWND control = CreateWindowExW(
      0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
      x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

HWND CreateCombo(HWND parent, HFONT font, int id,
                 int x, int y, int w, int h) {
  HWND control = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

HWND CreateCheck(HWND parent, HFONT font, int id, const wchar_t* text,
                 int x, int y, int w, int h) {
  HWND control = CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
      x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

HWND CreateButton(HWND parent, HFONT font, int id, const wchar_t* text,
                  int x, int y, int w, int h, bool default_button = false) {
  HWND control = CreateWindowExW(
      0, L"BUTTON", text,
      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
          (default_button ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
      x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
  SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return control;
}

struct LauncherState {
  LauncherSettings settings;
  std::filesystem::path config_path;
  std::vector<AdapterOption> adapters;
  HFONT font = nullptr;
  bool accepted = false;

  HWND resolution = nullptr;
  HWND display_mode = nullptr;
  HWND renderer = nullptr;
  HWND adapter = nullptr;
  HWND vsync = nullptr;
  HWND async_shaders = nullptr;
  HWND mnk = nullptr;
  HWND input_backend = nullptr;
  HWND language = nullptr;
  HWND show_at_startup = nullptr;

  int FindResolutionIndex(int width, int height) const {
    for (size_t i = 0; i < std::size(kResolutions); ++i) {
      if (kResolutions[i].width == width && kResolutions[i].height == height) {
        return static_cast<int>(i);
      }
    }
    return 0;
  }

  int FindLanguageIndex(int language_id) const {
    for (size_t i = 0; i < std::size(kLanguages); ++i) {
      if (kLanguages[i].language_id == language_id) {
        return static_cast<int>(i);
      }
    }
    return 0;
  }

  int FindAdapterComboIndex(int ordinal) const {
    if (ordinal < 0) {
      return 0;
    }
    for (size_t i = 0; i < adapters.size(); ++i) {
      if (adapters[i].ordinal == ordinal) {
        return static_cast<int>(i + 1);
      }
    }
    return 0;
  }

  void SetCheck(HWND control, bool checked) {
    SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
  }

  bool GetCheck(HWND control) const {
    return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
  }

  void FillControls(HWND hwnd) {
    font = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    CreateLabel(hwnd, font, L"DISPLAY", 28, 24, 180, 22);
    CreateLabel(hwnd, font, L"Resolution", 42, 58, 145, 24);
    resolution = CreateCombo(hwnd, font, kResolution, 195, 54, 280, 300);
    for (const auto& option : kResolutions) {
      SendMessageW(resolution, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(option.label));
    }

    CreateLabel(hwnd, font, L"Display mode", 42, 96, 145, 24);
    display_mode = CreateCombo(hwnd, font, kDisplayMode, 195, 92, 280, 120);
    SendMessageW(display_mode, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Fullscreen"));
    SendMessageW(display_mode, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Windowed"));

    CreateLabel(hwnd, font, L"Renderer", 42, 134, 145, 24);
    renderer = CreateCombo(hwnd, font, kRenderer, 195, 130, 280, 120);
    SendMessageW(renderer, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Direct3D 12"));
    SendMessageW(renderer, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Vulkan"));

    CreateLabel(hwnd, font, L"Graphics adapter", 42, 172, 145, 24);
    adapter = CreateCombo(hwnd, font, kAdapter, 195, 168, 470, 300);
    SendMessageW(adapter, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"Auto (high-performance)"));
    for (const auto& gpu : adapters) {
      std::wstring label = L"Adapter " + std::to_wstring(gpu.ordinal) + L" - " + gpu.name;
      if (gpu.software) {
        label += L" (software)";
      }
      SendMessageW(adapter, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(label.c_str()));
    }

    CreateLabel(hwnd, font, L"GRAPHICS", 28, 218, 180, 22);
    vsync = CreateCheck(hwnd, font, kVsync, L"VSync", 42, 250, 180, 26);
    async_shaders = CreateCheck(hwnd, font, kAsyncShaders,
                                L"Async shader compilation", 250, 250, 250, 26);

    CreateLabel(hwnd, font, L"INPUT", 28, 298, 180, 22);
    mnk = CreateCheck(hwnd, font, kMnk, L"Keyboard / mouse controls",
                      42, 330, 230, 26);
    CreateLabel(hwnd, font, L"Controller backend", 330, 332, 145, 24);
    input_backend = CreateCombo(hwnd, font, kInputBackend, 480, 328, 185, 120);
    SendMessageW(input_backend, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"SDL"));
    SendMessageW(input_backend, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"XInput"));

    CreateLabel(hwnd, font, L"LANGUAGE", 28, 378, 180, 22);
    CreateLabel(hwnd, font, L"Game language", 42, 412, 145, 24);
    language = CreateCombo(hwnd, font, kLanguage, 195, 408, 280, 220);
    for (const auto& option : kLanguages) {
      SendMessageW(language, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(option.label));
    }
    show_at_startup = CreateCheck(
        hwnd, font, kShowAtStartup, L"Show this launcher at startup",
        500, 408, 220, 26);

    CreateButton(hwnd, font, kDefaults, L"Defaults", 28, 470, 110, 34);
    CreateButton(hwnd, font, kExit, L"Exit", 520, 470, 90, 34);
    CreateButton(hwnd, font, kPlay, L"Play", 620, 470, 110, 34, true);

    ApplySettingsToControls();
  }

  void ApplySettingsToControls() {
    SendMessageW(resolution, CB_SETCURSEL,
                 FindResolutionIndex(settings.width, settings.height), 0);
    SendMessageW(display_mode, CB_SETCURSEL, settings.fullscreen ? 0 : 1, 0);
    SendMessageW(renderer, CB_SETCURSEL, std::clamp(settings.renderer, 0, 1), 0);
    SendMessageW(adapter, CB_SETCURSEL, FindAdapterComboIndex(settings.adapter), 0);
    SetCheck(vsync, settings.vsync);
    SetCheck(async_shaders, settings.async_shaders);
    SetCheck(mnk, settings.mnk);
    SendMessageW(input_backend, CB_SETCURSEL,
                 std::clamp(settings.input_backend, 0, 1), 0);
    SendMessageW(language, CB_SETCURSEL,
                 FindLanguageIndex(settings.language), 0);
    SetCheck(show_at_startup, settings.show_at_startup);
    EnableWindow(adapter, settings.renderer == 0);
  }

  void ReadControlsToSettings() {
    int resolution_index =
        static_cast<int>(SendMessageW(resolution, CB_GETCURSEL, 0, 0));
    resolution_index =
        std::clamp(resolution_index, 0, static_cast<int>(std::size(kResolutions)) - 1);
    settings.width = kResolutions[resolution_index].width;
    settings.height = kResolutions[resolution_index].height;

    settings.fullscreen = SendMessageW(display_mode, CB_GETCURSEL, 0, 0) == 0;
    settings.renderer =
        std::clamp(static_cast<int>(SendMessageW(renderer, CB_GETCURSEL, 0, 0)), 0, 1);

    int adapter_index =
        static_cast<int>(SendMessageW(adapter, CB_GETCURSEL, 0, 0));
    if (adapter_index <= 0 ||
        adapter_index > static_cast<int>(adapters.size())) {
      settings.adapter = -1;
    } else {
      settings.adapter = adapters[adapter_index - 1].ordinal;
    }

    settings.vsync = GetCheck(vsync);
    settings.async_shaders = GetCheck(async_shaders);
    settings.mnk = GetCheck(mnk);
    settings.input_backend =
        std::clamp(static_cast<int>(SendMessageW(input_backend, CB_GETCURSEL, 0, 0)), 0, 1);

    int language_index =
        std::clamp(static_cast<int>(SendMessageW(language, CB_GETCURSEL, 0, 0)),
                   0, static_cast<int>(std::size(kLanguages)) - 1);
    settings.language = kLanguages[language_index].language_id;
    settings.country = kLanguages[language_index].country_id;
    settings.show_at_startup = GetCheck(show_at_startup);
  }

  void ResetDefaults() {
    settings = DefaultSettings();
    ApplySettingsToControls();
  }
};

LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT message,
                                 WPARAM wparam, LPARAM lparam) {
  LauncherState* state =
      reinterpret_cast<LauncherState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    state = static_cast<LauncherState*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(state));
  }

  switch (message) {
    case WM_CREATE:
      if (state) {
        state->FillControls(hwnd);
      }
      return 0;

    case WM_COMMAND:
      if (!state) {
        break;
      }
      if (HIWORD(wparam) == CBN_SELCHANGE &&
          LOWORD(wparam) == kRenderer) {
        const int backend =
            static_cast<int>(SendMessageW(state->renderer, CB_GETCURSEL, 0, 0));
        EnableWindow(state->adapter, backend == 0);
        return 0;
      }
      if (HIWORD(wparam) == BN_CLICKED) {
        switch (LOWORD(wparam)) {
          case kDefaults:
            state->ResetDefaults();
            return 0;
          case kPlay:
            state->ReadControlsToSettings();
            SaveSettings(state->config_path, state->settings);
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
          case kExit:
            state->accepted = false;
            DestroyWindow(hwnd);
            return 0;
          default:
            break;
        }
      }
      break;

    case WM_CLOSE:
      if (state) {
        state->accepted = false;
      }
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool ShowLauncher(LauncherState& state) {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = LauncherWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kLauncherClass;
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ASURA_ICON));
  wc.hIconSm = wc.hIcon;

  RegisterClassExW(&wc);

  constexpr int width = 780;
  constexpr int height = 565;
  RECT rect{0, 0, width, height};
  AdjustWindowRectEx(&rect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                     FALSE, 0);
  const int window_width = rect.right - rect.left;
  const int window_height = rect.bottom - rect.top;
  const int x = (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2;
  const int y = (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2;

  HWND hwnd = CreateWindowExW(
      0, kLauncherClass, kLauncherTitle,
      WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
      x, y, window_width, window_height,
      nullptr, nullptr, instance, &state);
  if (!hwnd) {
    return true;
  }

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  SetForegroundWindow(hwnd);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(hwnd, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (state.font) {
    DeleteObject(state.font);
    state.font = nullptr;
  }
  return state.accepted;
}

}  // namespace

bool RunNativeLauncher(std::vector<std::string>& args) {
  SetProcessDPIAware();

  const bool force_show = HasArg(args, "--launcher") ||
                          (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool force_hide = HasArg(args, "--no_launcher");
  RemoveCustomLauncherArgs(args);

  LauncherState state;
  state.config_path = ConfigPath();
  state.adapters = EnumerateAdapters();

  bool has_config = std::filesystem::exists(state.config_path);
  if (has_config) {
    state.settings = LoadSettings(state.config_path);
  } else {
    const auto legacy_path = LegacyConfigPath();
    if (std::filesystem::exists(legacy_path)) {
      state.settings = LoadSettings(legacy_path);
      SaveSettings(state.config_path, state.settings);
      has_config = true;
    } else {
      state.settings = DefaultSettings();
    }
  }

  const bool should_show =
      !force_hide && (force_show || !has_config || state.settings.show_at_startup);
  if (should_show && !ShowLauncher(state)) {
    return false;
  }

  AppendSettingsArgs(args, state.settings, state.adapters);
  return true;
}

}  // namespace asura

#endif
