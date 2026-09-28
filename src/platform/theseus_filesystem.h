#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace theseus {

// PC-host filesystem service. This layer deliberately contains no guest/Xbox
// concepts and no ReXGlue types. Guest path compatibility lives in a separate
// adapter and may be removed independently later.
class NativeFileSystem final {
 public:
  [[nodiscard]] bool Exists(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] bool IsFile(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] bool IsDirectory(const std::filesystem::path& path) const noexcept;
  [[nodiscard]] std::optional<std::uintmax_t> FileSize(
      const std::filesystem::path& path) const noexcept;

  bool CreateDirectories(const std::filesystem::path& path) const noexcept;
  bool CopyFile(const std::filesystem::path& source,
                const std::filesystem::path& destination,
                bool overwrite = true) const noexcept;
  bool RemoveFile(const std::filesystem::path& path) const noexcept;

  [[nodiscard]] std::optional<std::vector<std::byte>> ReadAllBytes(
      const std::filesystem::path& path) const;
  [[nodiscard]] std::optional<std::string> ReadText(
      const std::filesystem::path& path) const;

  bool WriteAllBytes(const std::filesystem::path& path,
                     std::span<const std::byte> bytes) const;
  bool WriteText(const std::filesystem::path& path,
                 std::string_view text) const;
};

}  // namespace theseus
