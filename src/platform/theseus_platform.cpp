#include "platform/theseus_platform.h"

#include <algorithm>
#include <cctype>
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

bool IsDiscImage(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path)) {
    return false;
  }
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".iso" || ext == ".gdfx";
}

std::optional<std::filesystem::path> ResolveCandidate(
    const std::filesystem::path& candidate) {
  if (candidate.empty()) {
    return std::nullopt;
  }

  std::error_code ec;
  if (!std::filesystem::exists(candidate, ec)) {
    return std::nullopt;
  }

  if (IsDiscImage(candidate)) {
    return candidate;
  }

  if (!std::filesystem::is_directory(candidate, ec)) {
    return std::nullopt;
  }

  const std::filesystem::path direct_candidates[] = {
      candidate,
      candidate / "Data",
      candidate / "BCGame",
      candidate / "Data" / "BCGame",
      candidate / "extracted",
      candidate / "extracted" / "BCGame",
      candidate / "game_data",
  };

  for (const auto& dir : direct_candidates) {
    ec.clear();
    if (std::filesystem::is_regular_file(dir / "default.xex", ec)) {
      return dir;
    }
  }

  std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator it(candidate, ec);
       !ec && it != end; it.increment(ec)) {
    if (it->is_regular_file(ec) && IsDiscImage(it->path())) {
      return it->path();
    }
  }

  return std::nullopt;
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

  // Canonical release layout is Root/Data. Development/test packages are
  // allowed to live in Root/THESEUS_Txx next to a shared parent Data folder.
  const auto local_data = paths_.root / "Data";
  const auto sibling_data = paths_.root.parent_path() / "Data";
  std::error_code ec;
  if (std::filesystem::is_regular_file(local_data / "default.xex", ec) ||
      std::filesystem::is_directory(local_data / "Game", ec)) {
    paths_.data = local_data;
  } else {
    ec.clear();
    if (std::filesystem::is_regular_file(sibling_data / "default.xex", ec) ||
        std::filesystem::is_directory(sibling_data / "Game", ec)) {
      paths_.data = sibling_data;
    } else {
      paths_.data = local_data;
    }
  }

  paths_.game = paths_.data / "Game";
  paths_.content = paths_.game / "Content";
  paths_.cinematics = paths_.game / "Cinematics";
  paths_.toc = paths_.game / "Xbox360TOC.txt";
  paths_.dlc = paths_.root / "DLC";
  paths_.runtime = paths_.root / "Runtime";

  // Keep the runtime data local to this executable package. Test builds can
  // therefore share immutable game data while retaining isolated user state.
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

std::optional<std::filesystem::path> Platform::ResolveGameDataRoot(
    const std::filesystem::path& preferred,
    const std::filesystem::path& working_directory) const {
  const std::filesystem::path candidates[] = {
      preferred,
      paths_.data,
      paths_.root / "Data",
      paths_.root,
      paths_.root.parent_path() / "Data",
      paths_.root.parent_path(),
      paths_.user_data,
      working_directory,
  };

  for (const auto& candidate : candidates) {
    if (auto resolved = ResolveCandidate(candidate)) {
      return NormalizeRoot(*resolved);
    }
  }
  return std::nullopt;
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
