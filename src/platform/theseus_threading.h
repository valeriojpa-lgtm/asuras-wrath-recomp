#pragma once

namespace theseus {

class NativeThreadingService final {
 public:
  [[nodiscard]] constexpr bool host_sync_native() const noexcept {
    return true;
  }
};

}  // namespace theseus
