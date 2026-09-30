#pragma once

#include <cstdint>

namespace theseus {

enum class RenderResolutionPreset : std::int32_t {
  k720p = 0,
  k1080p = 1,
  k1440p = 2,
  kNative = 3,
  k4K = 4,
};

struct RenderResolution {
  std::int32_t width = 1280;
  std::int32_t height = 720;
  bool capped = false;
};

[[nodiscard]] RenderResolution ResolveRenderResolution(
    RenderResolutionPreset preset,
    std::int32_t output_width,
    std::int32_t output_height) noexcept;

[[nodiscard]] const char* RenderResolutionPresetName(
    RenderResolutionPreset preset) noexcept;

}  // namespace theseus
