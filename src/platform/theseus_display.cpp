#include "platform/theseus_display.h"

#include <algorithm>
#include <set>
#include <tuple>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace theseus {

bool NativeDisplayService::Enumerate() {
  monitors_.clear();

#if defined(_WIN32)
  DISPLAY_DEVICEW device{};
  device.cb = sizeof(device);

  for (DWORD device_index = 0;
       EnumDisplayDevicesW(nullptr, device_index, &device, 0);
       ++device_index) {
    if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) {
      device = {};
      device.cb = sizeof(device);
      continue;
    }

    DisplayMonitor monitor;
    monitor.index = static_cast<std::int32_t>(monitors_.size());
    monitor.device_name = device.DeviceName;
    monitor.display_name = device.DeviceString;
    monitor.primary = (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &current)) {
      monitor.current_mode = {
          static_cast<std::int32_t>(current.dmPelsWidth),
          static_cast<std::int32_t>(current.dmPelsHeight),
          static_cast<std::int32_t>(current.dmDisplayFrequency)};
    }

    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> unique_modes;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD mode_index = 0;
         EnumDisplaySettingsW(device.DeviceName, mode_index, &mode);
         ++mode_index) {
      const auto width = static_cast<std::int32_t>(mode.dmPelsWidth);
      const auto height = static_cast<std::int32_t>(mode.dmPelsHeight);
      const auto refresh = static_cast<std::int32_t>(mode.dmDisplayFrequency);
      if (width < 640 || height < 480 || refresh <= 0) {
        mode = {};
        mode.dmSize = sizeof(mode);
        continue;
      }
      unique_modes.emplace(width, height, refresh);
      mode = {};
      mode.dmSize = sizeof(mode);
    }

    monitor.modes.reserve(unique_modes.size());
    for (const auto& [width, height, refresh] : unique_modes) {
      monitor.modes.push_back({width, height, refresh});
    }
    std::sort(monitor.modes.begin(), monitor.modes.end(),
              [](const DisplayMode& a, const DisplayMode& b) {
                if (a.width != b.width) return a.width < b.width;
                if (a.height != b.height) return a.height < b.height;
                return a.refresh_hz < b.refresh_hz;
              });

    monitors_.push_back(std::move(monitor));
    device = {};
    device.cb = sizeof(device);
  }

  return !monitors_.empty();
#else
  return false;
#endif
}

const DisplayMonitor* NativeDisplayService::MonitorByIndex(
    std::int32_t index) const noexcept {
  if (index < 0 || index >= static_cast<std::int32_t>(monitors_.size())) {
    return nullptr;
  }
  return &monitors_[static_cast<std::size_t>(index)];
}

const DisplayMonitor* NativeDisplayService::PrimaryMonitor() const noexcept {
  const auto it = std::find_if(monitors_.begin(), monitors_.end(),
                               [](const DisplayMonitor& monitor) {
                                 return monitor.primary;
                               });
  return it != monitors_.end() ? &*it : nullptr;
}

}  // namespace theseus
