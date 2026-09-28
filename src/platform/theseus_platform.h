#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>

namespace theseus {

inline constexpr std::string_view kMilestone = "T01-platform-bootstrap";

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

  // Idempotent. T01 only establishes the portable PC-side boundary; all game
  // services still use their existing ReXGlue implementation.
  bool Bootstrap(const std::filesystem::path& executable_root);

  [[nodiscard]] bool initialized() const noexcept { return initialized_; }
  [[nodiscard]] const PortablePaths& paths() const noexcept { return paths_; }
  [[nodiscard]] Backend backend(Service service) const noexcept;

 private:
  Platform();

  PortablePaths paths_{};
  std::array<Backend, static_cast<std::size_t>(Service::kCount)> backends_{};
  bool initialized_ = false;
};

[[nodiscard]] std::string_view ToString(Backend backend) noexcept;

}  // namespace theseus
