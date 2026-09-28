#include "platform/theseus_saves.h"

#include <algorithm>
#include <format>

namespace theseus {
namespace {

bool LooksLikeHexDirectory(std::string_view name, std::size_t digits) {
  if (name.size() != digits) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  });
}

}  // namespace

bool NativeSaveSystem::Initialize(const std::filesystem::path& user_data_root,
                                  const std::filesystem::path& saves_root,
                                  const NativeFileSystem& files) {
  root_ = saves_root;

  if (!files.CreateDirectories(root_)) {
    return false;
  }

  // Best-effort migration for users coming from older ReXGlue layouts where
  // XAM content lived directly under UserData. This only moves directories
  // with a 16-hex-digit XUID name; unrelated UserData content is untouched.
  (void)MigrateLegacyContent(user_data_root, files);
  (void)WriteProfileManifest(files);

  initialized_ = true;
  return true;
}

bool NativeSaveSystem::MigrateLegacyContent(
    const std::filesystem::path& user_data_root,
    const NativeFileSystem& files) {
  std::error_code ec;
  if (!files.IsDirectory(user_data_root)) {
    return true;
  }

  for (std::filesystem::directory_iterator it(user_data_root, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }

    const auto name = it->path().filename().string();
    const bool xuid_tree = LooksLikeHexDirectory(name, 16);
    const bool title_profile_tree =
        LooksLikeHexDirectory(name, 8) &&
        files.IsDirectory(it->path() / "profile");
    if (!xuid_tree && !title_profile_tree) {
      continue;
    }

    const auto destination = root_ / it->path().filename();
    if (files.Exists(destination)) {
      continue;
    }

    ec.clear();
    std::filesystem::rename(it->path(), destination, ec);
    if (ec) {
      // Cross-volume / locked-file fallback: copy recursively, then keep the
      // legacy source if removal cannot be completed. Data safety wins.
      ec.clear();
      std::filesystem::copy(
          it->path(), destination,
          std::filesystem::copy_options::recursive |
              std::filesystem::copy_options::skip_existing,
          ec);
      if (ec) {
        return false;
      }
    }
  }

  return !ec;
}

bool NativeSaveSystem::WriteProfileManifest(
    const NativeFileSystem& files) const {
  const auto manifest = root_ / "TheseusProfile.ini";
  const auto text = std::format(
      "# Asura's Wrath - Theseus native profile\n"
      "[Profile]\n"
      "Name={}\n"
      "XUID={:016X}\n"
      "Schema=1\n",
      profile_.name, profile_.xuid);
  return files.WriteText(manifest, text);
}

bool NativeSaveSystem::IsFirstRun(const NativeFileSystem& files) const {
  if (!initialized_ || !files.IsDirectory(root_)) {
    return true;
  }

  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(
           root_, std::filesystem::directory_options::skip_permission_denied, ec),
       end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) {
      continue;
    }

    // Xbox 360 SavedGame content type is 00000001. We preserve that internal
    // package layout for compatibility while the host root itself is native.
    if (it->path().filename() == "00000001") {
      std::error_code child_ec;
      if (std::filesystem::directory_iterator(it->path(), child_ec) !=
          std::filesystem::directory_iterator()) {
        return false;
      }
    }

    // Asura may create title-profile/options data before a SavedGame package.
    // Treat that as an initialized game profile too, matching the actual
    // first-run behavior observed on the validated T04 build.
    if (it->path().filename() == "profile") {
      std::error_code child_ec;
      if (std::filesystem::directory_iterator(it->path(), child_ec) !=
          std::filesystem::directory_iterator()) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace theseus
