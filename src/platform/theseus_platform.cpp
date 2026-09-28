#include "platform/theseus_platform.h"

#include <system_error>

namespace theseus {
namespace {

std::filesystem::path NormalizeRoot(const std::filesystem::path& root) {
  std::error_code ec;
  auto normalized = std::filesystem::weakly_canonical(root, ec);
  return ec ? root.lexically_normal() : normalized;
}

void CreateDirectoryBestEffort(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
}

}  // namespace

Platform& Platform::Instance() {
  static Platform platform;
  return platform;
}

Platform::Platform() {
  // The defining rule of T01: the boundary exists, but behavior is still the
  // frozen RUN04 behavior. Services become native one at a time in later runs.
  backends_.fill(Backend::kReXGlue);
}

bool Platform::Bootstrap(const std::filesystem::path& executable_root) {
  if (initialized_) {
    return true;
  }

  paths_.root = NormalizeRoot(executable_root);
  paths_.data = paths_.root / "Data";
  paths_.game = paths_.data / "Game";
  paths_.content = paths_.game / "Content";
  paths_.cinematics = paths_.game / "Cinematics";
  paths_.toc = paths_.game / "Xbox360TOC.txt";
  paths_.dlc = paths_.root / "DLC";
  paths_.runtime = paths_.root / "Runtime";

  // Keep RUN04's existing portable location and cache spelling so T01 does
  // not intentionally alter runtime behavior.
  paths_.user_data = paths_.root / "UserData";
  paths_.cache = paths_.user_data / "cache";

  // Reserved native-PC locations. Creating these empty directories is harmless
  // and gives later Theseus services stable destinations without AppData,
  // Documents, registry state, or an installer.
  paths_.config = paths_.user_data / "Config";
  paths_.logs = paths_.user_data / "Logs";
  paths_.saves = paths_.user_data / "Saves";

  CreateDirectoryBestEffort(paths_.user_data);
  CreateDirectoryBestEffort(paths_.cache);
  CreateDirectoryBestEffort(paths_.config);
  CreateDirectoryBestEffort(paths_.logs);
  CreateDirectoryBestEffort(paths_.saves);

  initialized_ = true;
  return true;
}

Backend Platform::backend(Service service) const noexcept {
  const auto index = static_cast<std::size_t>(service);
  if (index >= backends_.size()) {
    return Backend::kReXGlue;
  }
  return backends_[index];
}

std::string_view ToString(Backend backend) noexcept {
  switch (backend) {
    case Backend::kNative:
      return "native";
    case Backend::kReXGlue:
    default:
      return "rexglue";
  }
}

}  // namespace theseus
