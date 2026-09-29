#pragma once

#include <cstdint>

namespace theseus {

struct TimingPolicyState {
  std::uint64_t guest_frequency = 50000000ull;
  double time_scale = 1.0;
};

class NativeTimingPolicy final {
 public:
  void Configure(const TimingPolicyState& state) noexcept { state_ = state; }

  [[nodiscard]] const TimingPolicyState& state() const noexcept {
    return state_;
  }

 private:
  TimingPolicyState state_{};
};

}  // namespace theseus
