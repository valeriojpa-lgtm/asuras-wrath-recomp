#pragma once

#include <cstdint>

namespace theseus {

struct AudioPolicyState {
  bool mute = false;

  // ReXGlue currently supports 4..64 queued frames. Theseus owns the stable
  // PC-facing value while the temporary audio bridge consumes it.
  std::int32_t queued_frames = 8;
};

class NativeAudioPolicy final {
 public:
  void Configure(const AudioPolicyState& state) noexcept {
    state_ = state;
    if (state_.queued_frames < 4) state_.queued_frames = 4;
    if (state_.queued_frames > 64) state_.queued_frames = 64;
  }

  [[nodiscard]] const AudioPolicyState& state() const noexcept {
    return state_;
  }

 private:
  AudioPolicyState state_{};
};

}  // namespace theseus
