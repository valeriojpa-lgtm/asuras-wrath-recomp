#pragma once

#include <filesystem>

namespace theseus {

struct Config {
  int width = 1280;
  int height = 720;
  bool fullscreen = true;

  // Kept as stable Theseus values so the UI is independent of the current
  // runtime adapter: 0 = D3D12, 1 = Vulkan.
  int renderer = 0;
  int adapter = -1;
  bool vsync = false;
  bool async_shaders = false;

  bool mnk = true;
  int input_backend = 0;  // 0 = SDL, 1 = XInput.

  int language = 1;
  int country = 103;
  bool show_at_startup = true;
};

[[nodiscard]] bool LoadConfig(const std::filesystem::path& path,
                              Config& config);
[[nodiscard]] bool SaveConfig(const std::filesystem::path& path,
                              const Config& config);

}  // namespace theseus
