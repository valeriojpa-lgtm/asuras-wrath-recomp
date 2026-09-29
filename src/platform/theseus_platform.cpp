#include "platform/theseus_platform.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

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

std::filesystem::path ProcessExecutableFolder() {
#if defined(_WIN32)
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length && length < buffer.size()) {
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::vector<char> buffer(size + 1, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
    return std::filesystem::path(buffer.data()).parent_path();
  }
#else
  std::vector<char> buffer(4096, '\0');
  const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length > 0) {
    buffer[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path();
  }
#endif

  std::error_code ec;
  return std::filesystem::current_path(ec);
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
  backends_.fill(Backend::kReXGlue);

  // T03: host filesystem ownership moved to Theseus.
  backends_[static_cast<std::size_t>(Service::kFileSystem)] = Backend::kNative;

  // T04: host save/profile policy is Theseus-owned. Guest XAM calls remain a
  // compatibility bridge while the recompiled game still speaks the Xbox ABI.
  backends_[static_cast<std::size_t>(Service::kSaves)] = Backend::kNative;

  // T05: input policy/configuration belongs to Theseus. Physical SDL/XInput
  // drivers and the guest XAM ABI remain temporary compatibility bridges.
  backends_[static_cast<std::size_t>(Service::kInput)] = Backend::kNative;

  // T06: audio policy/configuration belongs to Theseus. XMA decode, the guest
  // XAudio/XMA ABI and SDL sample submission remain temporary bridges.
  backends_[static_cast<std::size_t>(Service::kAudio)] = Backend::kNative;

  // T07: host/guest clock policy and conversion are project-owned. The Xbox
  // kernel timing exports remain a temporary ABI bridge inside rexruntime.
  backends_[static_cast<std::size_t>(Service::kTiming)] = Backend::kNative;
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

  if (!saves_.Initialize(paths_.user_data, paths_.saves, files_)) {
    return false;
  }

  initialized_ = true;
  return true;
}

bool Platform::BootstrapFromProcess() {
  return Bootstrap(ProcessExecutableFolder());
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
