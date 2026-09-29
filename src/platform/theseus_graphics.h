#pragma once

#include <cstdint>

namespace theseus {

enum class GraphicsBackend : std::int32_t {
  kD3D12 = 0,
  kVulkan = 1,
};

struct GraphicsPolicyState {
  GraphicsBackend backend = GraphicsBackend::kD3D12;

  // -1 means automatic/default adapter selection. Non-negative values are
  // stable host adapter ordinals owned by Theseus; the temporary ReXGlue
  // compatibility bridge consumes the value for D3D12.
  std::int32_t adapter = -1;

  bool vsync = false;
  bool async_shaders = false;
};

class NativeGraphicsPolicy final {
 public:
  void Configure(const GraphicsPolicyState& state) noexcept {
    state_ = state;
    if (state_.adapter < -1) {
      state_.adapter = -1;
    }
  }

  [[nodiscard]] const GraphicsPolicyState& state() const noexcept {
    return state_;
  }

 private:
  GraphicsPolicyState state_{};
};

}  // namespace theseus
