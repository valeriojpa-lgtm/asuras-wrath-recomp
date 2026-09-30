#include "platform/theseus_render_resolution.h"

namespace theseus {

RenderResolutionPolicy ResolveRenderResolution(
    RenderResolutionPreset preset) noexcept {
  switch (preset) {
    case RenderResolutionPreset::k1440p:
      return {2, 2560, 1440};
    case RenderResolutionPreset::k4K:
      return {3, 3840, 2160};
    case RenderResolutionPreset::kOriginal720p:
    default:
      return {1, 1280, 720};
  }
}

const char* RenderResolutionPresetName(RenderResolutionPreset preset) noexcept {
  switch (preset) {
    case RenderResolutionPreset::k1440p:
      return "1440p";
    case RenderResolutionPreset::k4K:
      return "4k";
    case RenderResolutionPreset::kOriginal720p:
    default:
      return "720p";
  }
}

}  // namespace theseus
