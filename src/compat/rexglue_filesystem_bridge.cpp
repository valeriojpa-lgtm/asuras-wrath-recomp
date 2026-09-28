#include "compat/rexglue_filesystem_bridge.h"

#include <memory>
#include <span>
#include <string_view>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/logging.h>
#include <rex/runtime.h>

namespace asura::compat {
namespace {

bool ExtractDiscEntry(rex::filesystem::Entry* entry,
                      const std::filesystem::path& target_path,
                      const theseus::NativeFileSystem& files) {
  if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
    if (!files.CreateDirectories(target_path)) {
      return false;
    }
    for (const auto& child : entry->children()) {
      if (!ExtractDiscEntry(child.get(), target_path / child->name(), files)) {
        return false;
      }
    }
    return true;
  }

  auto* disc_entry = static_cast<rex::filesystem::DiscImageEntry*>(entry);
  if (!disc_entry || !disc_entry->mmap()) {
    return false;
  }

  const auto* data = reinterpret_cast<const std::byte*>(
      disc_entry->mmap()->data() + disc_entry->data_offset());
  const auto size = static_cast<std::size_t>(disc_entry->data_size());
  return files.WriteAllBytes(target_path, std::span<const std::byte>(data, size));
}

}  // namespace

bool MountPcDataLayout(rex::Runtime* runtime,
                       const std::filesystem::path& game_data_root,
                       const std::filesystem::path& user_data_root,
                       const theseus::PortablePaths& portable_paths,
                       const theseus::NativeFileSystem& files) {
  if (!runtime || !runtime->file_system()) {
    return false;
  }

  auto* vfs = runtime->file_system();

  // Prefer the canonical Theseus layout. If an explicit legacy data root was
  // selected, fall back to the same clean Game/{Content,Cinematics} shape
  // relative to that root.
  auto content_dir = portable_paths.content;
  auto cinematics_dir = portable_paths.cinematics;
  auto toc_source = portable_paths.toc;

  const auto explicit_content = game_data_root / "Game" / "Content";
  const auto explicit_cinematics = game_data_root / "Game" / "Cinematics";
  const auto explicit_toc = game_data_root / "Game" / "Xbox360TOC.txt";

  if (files.IsDirectory(explicit_content)) {
    content_dir = explicit_content;
  }
  if (files.IsDirectory(explicit_cinematics)) {
    cinematics_dir = explicit_cinematics;
  }
  if (files.IsFile(explicit_toc)) {
    toc_source = explicit_toc;
  }

  const bool has_content = files.IsDirectory(content_dir);
  const bool has_cinematics = files.IsDirectory(cinematics_dir);

  // Legacy/extracted Xbox-shaped layouts already expose their native guest
  // names and do not need clean-layout aliases.
  if (!has_content && !has_cinematics) {
    return true;
  }

  const auto compat_bcgame = user_data_root / "vfs" / "BCGame";
  if (!files.CreateDirectories(compat_bcgame / "CookedXbox360") ||
      !files.CreateDirectories(compat_bcgame / "Movies")) {
    REXLOG_ERROR("Failed to create Theseus guest filesystem bridge");
    return false;
  }

  if (!files.IsFile(toc_source)) {
    const std::filesystem::path toc_candidates[] = {
        game_data_root / "Xbox360TOC.txt",
        game_data_root / "BCGame" / "Xbox360TOC.txt",
        game_data_root.parent_path() / "BCGame" / "Xbox360TOC.txt",
    };
    for (const auto& candidate : toc_candidates) {
      if (files.IsFile(candidate)) {
        toc_source = candidate;
        break;
      }
    }
  }

  const auto toc_compat = compat_bcgame / "Xbox360TOC.txt";
  if (files.IsFile(toc_source)) {
    if (!files.CopyFile(toc_source, toc_compat, true)) {
      REXLOG_ERROR("Failed to mirror Xbox360TOC.txt into guest bridge");
    } else {
      REXLOG_INFO("Theseus TOC source: {}", toc_source.string());
    }
  } else {
    REXLOG_WARN("Xbox360TOC.txt not found by Theseus native filesystem");
  }

  auto register_mount = [&](const std::filesystem::path& host_dir,
                            std::string_view guest_mount) -> bool {
    if (!files.IsDirectory(host_dir)) {
      REXLOG_ERROR("Theseus mount source missing: {}", host_dir.string());
      return false;
    }

    auto device = std::make_unique<rex::filesystem::HostPathDevice>(
        guest_mount, host_dir, true);
    if (!device->Initialize()) {
      REXLOG_ERROR("Failed to initialize guest bridge {} -> {}",
                   guest_mount, host_dir.string());
      return false;
    }
    if (!vfs->RegisterDevice(std::move(device))) {
      REXLOG_ERROR("Failed to register guest bridge {}", guest_mount);
      return false;
    }

    REXLOG_INFO("Theseus guest bridge: {} -> {}", guest_mount,
                host_dir.string());
    return true;
  };

  bool ok = true;
  ok &= register_mount(compat_bcgame,
                       "\\Device\\Harddisk0\\Partition1\\BCGame");
  if (has_content) {
    ok &= register_mount(
        content_dir,
        "\\Device\\Harddisk0\\Partition1\\BCGame\\CookedXbox360");
  }
  if (has_cinematics) {
    ok &= register_mount(
        cinematics_dir,
        "\\Device\\Harddisk0\\Partition1\\BCGame\\Movies");
  }
  return ok;
}

bool ExtractDiscImageToDirectory(
    const std::filesystem::path& image_path,
    const std::filesystem::path& destination,
    const theseus::NativeFileSystem& files) {
  if (!files.CreateDirectories(destination)) {
    return false;
  }

  REXLOG_INFO("Importing disc image {} to {}...", image_path.string(),
              destination.string());

  rex::filesystem::DiscImageDevice device("game:", image_path);
  if (!device.Initialize()) {
    REXLOG_ERROR("Failed to initialize legacy disc-image importer: {}",
                 image_path.string());
    return false;
  }

  const auto* root = device.root();
  if (!root) {
    REXLOG_ERROR("Disc-image root entry is null");
    return false;
  }

  for (const auto& child : root->children()) {
    if (!ExtractDiscEntry(child.get(), destination / child->name(), files)) {
      REXLOG_ERROR("Failed to import disc-image entry {}", child->name());
      return false;
    }
  }

  REXLOG_INFO("Successfully imported disc image to {}", destination.string());
  return true;
}

}  // namespace asura::compat
