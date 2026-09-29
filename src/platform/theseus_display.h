#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace theseus {

struct DisplayMode {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t refresh_hz = 0;

  friend bool operator==(const DisplayMode&, const DisplayMode&) = default;
};

struct DisplayMonitor {
  std::int32_t index = 0;  // 0-based native index.
  std::wstring device_name;
  std::wstring display_name;
  bool primary = false;
  DisplayMode current_mode{};
  std::vector<DisplayMode> modes;
};

class NativeDisplayService final {
 public:
  [[nodiscard]] bool Enumerate();
  [[nodiscard]] const std::vector<DisplayMonitor>& monitors() const noexcept {
    return monitors_;
  }

  [[nodiscard]] const DisplayMonitor* MonitorByIndex(std::int32_t index) const noexcept;
  [[nodiscard]] const DisplayMonitor* PrimaryMonitor() const noexcept;

 private:
  std::vector<DisplayMonitor> monitors_;
};

}  // namespace theseus
