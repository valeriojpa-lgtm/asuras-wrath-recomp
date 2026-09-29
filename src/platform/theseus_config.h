#pragma once

#include <filesystem>

#include "platform/theseus_audio.h"
#include "platform/theseus_input.h"

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
  bool mouse_look = false;
  double mouse_sensitivity = 1.0;
  bool hide_cursor_in_game = true;
  InputBindings keybinds{};

  bool audio_mute = false;
  int audio_queued_frames = 8;

  [[nodiscard]] AudioPolicyState MakeAudioPolicy() const {
    AudioPolicyState policy;
    policy.mute = audio_mute;
    policy.queued_frames = audio_queued_frames;
    return policy;
  }

  [[nodiscard]] InputPolicyState MakeInputPolicy() const {
    InputPolicyState policy;
    policy.backend = input_backend == 1 ? InputBackend::kXInput
                                        : InputBackend::kSDL;
    policy.keyboard_mouse = mnk;
    policy.mouse_look = mouse_look;
    policy.mouse_sensitivity = mouse_sensitivity;
    policy.hide_cursor_in_game = hide_cursor_in_game;
    policy.bindings = keybinds;
    return policy;
  }

  int language = 1;
  int country = 103;
  bool show_at_startup = true;
};

[[nodiscard]] bool LoadConfig(const std::filesystem::path& path,
                              Config& config);
[[nodiscard]] bool SaveConfig(const std::filesystem::path& path,
                              const Config& config);

}  // namespace theseus
