#pragma once

#include <cstdint>

namespace theseus {

enum class RenderResolutionPreset : std::int32_t {
  kOriginal720p = 0,
  k1440p = 1,
  k4K = 2,
};

struct RenderResolutionPolicy {
  std::int32_t scale = 1;
  std::int32_t reference_width = 1280;
  std::int32_t reference_height = 720;
};

[[nodiscard]] RenderResolutionPolicy ResolveRenderResolution(
    RenderResolutionPreset preset) noexcept;

[[nodiscard]] const char* RenderResolutionPresetName(
    RenderResolutionPreset preset) noexcept;

}  // namespace theseus
