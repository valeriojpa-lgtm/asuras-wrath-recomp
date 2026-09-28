#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

#include "platform/theseus_filesystem.h"
#include "platform/theseus_saves.h"

namespace theseus {

inline constexpr std::string_view kMilestone = "T04-native-saves-profile";

enum class Backend {
  kReXGlue,
  kNative,
};

enum class Service : std::size_t {
  kFileSystem = 0,
  kInput,
  kSaves,
  kVideo,
  kAudio,
  kTiming,
  kThreading,
  kGraphics,
  kCount,
};

struct PortablePaths {
  std::filesystem::path root;
  std::filesystem::path data;
  std::filesystem::path game;
  std::filesystem::path content;
  std::filesystem::path cinematics;
  std::filesystem::path toc;
  std::filesystem::path dlc;
  std::filesystem::path runtime;
  std::filesystem::path user_data;
  std::filesystem::path cache;
  std::filesystem::path config;
  std::filesystem::path logs;
  std::filesystem::path saves;
};

class Platform final {
 public:
  static Platform& Instance();

  // Idempotent. Establishes the portable PC-side boundary and resolves the
  // canonical/sibling Data layout without depending on the runtime backend.
  bool Bootstrap(const std::filesystem::path& executable_root);
  bool BootstrapFromProcess();

  [[nodiscard]] std::optional<std::filesystem::path> ResolveGameDataRoot(
      const std::filesystem::path& preferred = {},
      const std::filesystem::path& working_directory = {}) const;

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] const PortablePaths& paths() const noexcept { return paths_; }
  [[nodiscard]] NativeFileSystem& files() noexcept { return files_; }
  [[nodiscard]] const NativeFileSystem& files() const noexcept { return files_; }
  [[nodiscard]] NativeSaveSystem& saves() noexcept { return saves_; }
  [[nodiscard]] const NativeSaveSystem& saves() const noexcept { return saves_; }
  [[nodiscard]] Backend backend(Service service) const noexcept;

 private:
  Platform();

  PortablePaths paths_{};
  NativeFileSystem files_{};
  NativeSaveSystem saves_{};
  std::array<Backend, static_cast<std::size_t>(Service::kCount)> backends_{};
  bool initialized_ = false;
};

[[nodiscard]] std::string_view ToString(Backend backend) noexcept;

}  // namespace theseus
