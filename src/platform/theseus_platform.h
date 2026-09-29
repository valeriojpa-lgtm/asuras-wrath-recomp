#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>

#include "platform/theseus_audio.h"
#include "platform/theseus_filesystem.h"
#include "platform/theseus_input.h"
#include "platform/theseus_saves.h"
#include "platform/theseus_timing.h"
#include "platform/theseus_threading.h"

namespace theseus {

inline constexpr std::string_view kMilestone = "T09.2-heap-corruption-probe";

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
  [[nodiscard]] NativeInputPolicy& input() noexcept { return input_; }
  [[nodiscard]] const NativeInputPolicy& input() const noexcept { return input_; }
  [[nodiscard]] NativeAudioPolicy& audio() noexcept { return audio_; }
  [[nodiscard]] const NativeAudioPolicy& audio() const noexcept { return audio_; }
  [[nodiscard]] NativeTimingPolicy& timing() noexcept { return timing_; }
  [[nodiscard]] const NativeTimingPolicy& timing() const noexcept { return timing_; }
  [[nodiscard]] NativeThreadingService& threading() noexcept { return threading_; }
  [[nodiscard]] const NativeThreadingService& threading() const noexcept { return threading_; }
  [[nodiscard]] Backend backend(Service service) const noexcept;

 private:
  Platform();

  PortablePaths paths_{};
  NativeFileSystem files_{};
  NativeSaveSystem saves_{};
  NativeInputPolicy input_{};
  NativeAudioPolicy audio_{};
  NativeTimingPolicy timing_{};
  NativeThreadingService threading_{};
  std::array<Backend, static_cast<std::size_t>(Service::kCount)> backends_{};
  bool initialized_ = false;
};

[[nodiscard]] std::string_view ToString(Backend backend) noexcept;

}  // namespace theseus
