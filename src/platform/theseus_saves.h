#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "platform/theseus_filesystem.h"

namespace theseus {

struct NativeProfile {
  // Keep the compatibility identity stable so saves created by earlier
  // ReXGlue-based builds remain discoverable after migration.
  std::uint64_t xuid = 0xB13EBABEBABEBABEull;
  std::string name = "User";
};

class NativeSaveSystem final {
 public:
  bool Initialize(const std::filesystem::path& user_data_root,
                  const std::filesystem::path& saves_root,
                  const NativeFileSystem& files);

  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }
  [[nodiscard]] const NativeProfile& profile() const noexcept {
    return profile_;
  }
  [[nodiscard]] bool initialized() const noexcept { return initialized_; }

  // True when no Xbox SavedGame package exists under the native save root.
  [[nodiscard]] bool IsFirstRun(const NativeFileSystem& files) const;

 private:
  bool MigrateLegacyContent(const std::filesystem::path& user_data_root,
                            const NativeFileSystem& files);
  bool WriteProfileManifest(const NativeFileSystem& files) const;

  std::filesystem::path root_;
  NativeProfile profile_{};
  bool initialized_ = false;
};

}  // namespace theseus
