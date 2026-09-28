#include "platform/theseus_filesystem.h"

#include <fstream>
#include <iterator>

namespace theseus {

bool NativeFileSystem::Exists(const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec;
}

bool NativeFileSystem::IsFile(const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  return std::filesystem::is_regular_file(path, ec) && !ec;
}

bool NativeFileSystem::IsDirectory(
    const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  return std::filesystem::is_directory(path, ec) && !ec;
}

std::optional<std::uintmax_t> NativeFileSystem::FileSize(
    const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    return std::nullopt;
  }
  return size;
}

bool NativeFileSystem::CreateDirectories(
    const std::filesystem::path& path) const noexcept {
  if (path.empty()) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec;
}

bool NativeFileSystem::CopyFile(const std::filesystem::path& source,
                                const std::filesystem::path& destination,
                                bool overwrite) const noexcept {
  std::error_code ec;
  if (!destination.parent_path().empty()) {
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
      return false;
    }
  }

  const auto options = overwrite
      ? std::filesystem::copy_options::overwrite_existing
      : std::filesystem::copy_options::none;
  std::filesystem::copy_file(source, destination, options, ec);
  return !ec;
}

bool NativeFileSystem::RemoveFile(
    const std::filesystem::path& path) const noexcept {
  std::error_code ec;
  const bool removed = std::filesystem::remove(path, ec);
  return !ec && (removed || !std::filesystem::exists(path, ec));
}

std::optional<std::vector<std::byte>> NativeFileSystem::ReadAllBytes(
    const std::filesystem::path& path) const {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in.is_open()) {
    return std::nullopt;
  }

  const auto end = in.tellg();
  if (end < 0) {
    return std::nullopt;
  }

  std::vector<std::byte> data(static_cast<std::size_t>(end));
  in.seekg(0, std::ios::beg);
  if (!data.empty()) {
    in.read(reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
  }
  if (!in.good() && !in.eof()) {
    return std::nullopt;
  }
  return data;
}

std::optional<std::string> NativeFileSystem::ReadText(
    const std::filesystem::path& path) const {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

bool NativeFileSystem::WriteAllBytes(const std::filesystem::path& path,
                                     std::span<const std::byte> bytes) const {
  if (!CreateDirectories(path.parent_path())) {
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  if (!bytes.empty()) {
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  return out.good();
}

bool NativeFileSystem::WriteText(const std::filesystem::path& path,
                                 std::string_view text) const {
  const auto bytes =
      std::as_bytes(std::span<const char>(text.data(), text.size()));
  return WriteAllBytes(path, bytes);
}

}  // namespace theseus
