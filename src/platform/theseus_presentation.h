#pragma once

#include <cstdint>

namespace theseus {

struct PresentationPolicyState {
  std::int32_t width = 1280;
  std::int32_t height = 720;
  bool fullscreen = true;
  std::int32_t monitor = 0;
};

class NativePresentationPolicy final {
 public:
  void Configure(const PresentationPolicyState& state) noexcept {
    state_ = state;
    if (state_.width < 640) state_.width = 640;
    if (state_.height < 480) state_.height = 480;
    if (state_.width > 8192) state_.width = 8192;
    if (state_.height > 8192) state_.height = 8192;
    if (state_.monitor < 0) state_.monitor = 0;
  }

  [[nodiscard]] const PresentationPolicyState& state() const noexcept {
    return state_;
  }

 private:
  PresentationPolicyState state_{};
};

}  // namespace theseus
