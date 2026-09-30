#include "platform/theseus_render_resolution.h"

#include <algorithm>
#include <cmath>

namespace theseus {
namespace {

constexpr std::int32_t kGuestMinWidth = 640;
constexpr std::int32_t kGuestMinHeight = 480;
constexpr std::int32_t kGuestMaxDimension = 0x0FFF;  // Xbox video-mode ABI.

std::int32_t Even(std::int32_t value) noexcept {
  return std::max<std::int32_t>(2, value & ~1);
}

RenderResolution FitAspect(std::int32_t output_width,
                           std::int32_t output_height,
                           std::int32_t target_height) noexcept {
  output_width = std::max(output_width, 1);
  output_height = std::max(output_height, 1);

  const double aspect =
      static_cast<double>(output_width) / static_cast<double>(output_height);

  std::int32_t height = std::max(target_height, kGuestMinHeight);
  std::int32_t width =
      Even(static_cast<std::int32_t>(std::lround(height * aspect)));

  bool capped = false;
  if (width > kGuestMaxDimension) {
    width = Even(kGuestMaxDimension);
    height = Even(static_cast<std::int32_t>(std::lround(width / aspect)));
    capped = true;
  }
  if (height > kGuestMaxDimension) {
    height = Even(kGuestMaxDimension);
    width = Even(static_cast<std::int32_t>(std::lround(height * aspect)));
    capped = true;
  }

  width = std::clamp(width, kGuestMinWidth, kGuestMaxDimension);
  height = std::clamp(height, kGuestMinHeight, kGuestMaxDimension);
  return {width, height, capped};
}

}  // namespace

RenderResolution ResolveRenderResolution(RenderResolutionPreset preset,
                                         std::int32_t output_width,
                                         std::int32_t output_height) noexcept {
  output_width = std::max(output_width, 1);
  output_height = std::max(output_height, 1);

  if (preset == RenderResolutionPreset::kNative) {
    const double aspect =
        static_cast<double>(output_width) / static_cast<double>(output_height);
    std::int32_t width = output_width;
    std::int32_t height = output_height;
    bool capped = false;

    if (width > kGuestMaxDimension) {
      width = Even(kGuestMaxDimension);
      height = Even(static_cast<std::int32_t>(std::lround(width / aspect)));
      capped = true;
    }
    if (height > kGuestMaxDimension) {
      height = Even(kGuestMaxDimension);
      width = Even(static_cast<std::int32_t>(std::lround(height * aspect)));
      capped = true;
    }

    return {
        std::clamp(width, kGuestMinWidth, kGuestMaxDimension),
        std::clamp(height, kGuestMinHeight, kGuestMaxDimension),
        capped};
  }

  std::int32_t target_height = 720;
  switch (preset) {
    case RenderResolutionPreset::k1080p:
      target_height = 1080;
      break;
    case RenderResolutionPreset::k1440p:
      target_height = 1440;
      break;
    case RenderResolutionPreset::k4K:
      target_height = 2160;
      break;
    case RenderResolutionPreset::k720p:
    default:
      target_height = 720;
      break;
  }

  return FitAspect(output_width, output_height, target_height);
}

const char* RenderResolutionPresetName(RenderResolutionPreset preset) noexcept {
  switch (preset) {
    case RenderResolutionPreset::k720p:
      return "720p";
    case RenderResolutionPreset::k1080p:
      return "1080p";
    case RenderResolutionPreset::k1440p:
      return "1440p";
    case RenderResolutionPreset::kNative:
      return "native";
    case RenderResolutionPreset::k4K:
      return "4k";
    default:
      return "native";
  }
}

}  // namespace theseus
