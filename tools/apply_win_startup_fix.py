from pathlib import Path


def patch_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        print(f"{label}: already applied")
        return
    if old not in text:
        raise SystemExit(f"{label}: expected source block not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    print(f"{label}: applied")


# 1) Windows startup: avoid UCRT _get_wpgmptr fail-fast seen on Windows 11.
filesystem_win = Path("tools/rexglue/src/core/filesystem_win.cpp")
old_startup = """std::filesystem::path GetExecutablePath() {
  wchar_t* path;
  auto error = _get_wpgmptr(&path);
  return !error ? std::filesystem::path(path) : std::filesystem::path();
}"""

new_startup = """std::filesystem::path GetExecutablePath() {
  std::wstring path(32768, L'\\0');
  DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || length >= path.size()) {
    return std::filesystem::path();
  }
  path.resize(length);
  return std::filesystem::path(path);
}"""

patch_once(filesystem_win, old_startup, new_startup, "Windows executable path fix")


# 2) VFS aliases: when symbolic-link prefixes overlap, always use the most
# specific (longest) prefix. This lets Asura expose BCGame as a compatibility
# root while redirecting CookedXbox360 and Movies to clean PC folder names.
vfs = Path("tools/rexglue/src/filesystem/virtual_file_system.cpp")

old_find = """bool VirtualFileSystem::FindSymbolicLink(const std::string_view path, std::string& target) {
  auto it = std::find_if(symlinks_.cbegin(), symlinks_.cend(), [&](const auto& s) {
    return rex::string::utf8_starts_with_case(path, s.first);
  });
  if (it == symlinks_.cend()) {
    return false;
  }
  target = (*it).second;
  return true;
}"""

new_find = """bool VirtualFileSystem::FindSymbolicLink(const std::string_view path, std::string& target) {
  auto best = symlinks_.cend();
  size_t best_length = 0;
  for (auto it = symlinks_.cbegin(); it != symlinks_.cend(); ++it) {
    if (it->first.size() > best_length &&
        rex::string::utf8_starts_with_case(path, it->first)) {
      best = it;
      best_length = it->first.size();
    }
  }
  if (best == symlinks_.cend()) {
    return false;
  }
  target = best->second;
  return true;
}"""

old_resolve = """bool VirtualFileSystem::ResolveSymbolicLink(const std::string_view path, std::string& result) {
  result = path;
  bool was_resolved = false;
  while (true) {
    auto it = std::find_if(symlinks_.cbegin(), symlinks_.cend(), [&](const auto& s) {
      return rex::string::utf8_starts_with_case(result, s.first);
    });
    if (it == symlinks_.cend()) {
      break;
    }
    // Found symlink!
    auto target_path = (*it).second;
    auto relative_path = result.substr((*it).first.size());
    result = target_path + relative_path;
    was_resolved = true;
  }
  return was_resolved;
}"""

new_resolve = """bool VirtualFileSystem::ResolveSymbolicLink(const std::string_view path, std::string& result) {
  result = path;
  bool was_resolved = false;
  while (true) {
    auto best = symlinks_.cend();
    size_t best_length = 0;
    for (auto it = symlinks_.cbegin(); it != symlinks_.cend(); ++it) {
      if (it->first.size() > best_length &&
          rex::string::utf8_starts_with_case(result, it->first)) {
        best = it;
        best_length = it->first.size();
      }
    }
    if (best == symlinks_.cend()) {
      break;
    }

    auto target_path = best->second;
    auto relative_path = result.substr(best->first.size());
    result = target_path + relative_path;
    was_resolved = true;
  }
  return was_resolved;
}"""

patch_once(vfs, old_find, new_find, "VFS longest-prefix lookup")
patch_once(vfs, old_resolve, new_resolve, "VFS longest-prefix resolution")


# 3) ReXApp logging: OnConfigurePaths may redirect user_data_root (Asura does
# this for portable UserData), but SetupEnvironment kept using the pre-hook
# user_dir when choosing the default log directory. Synchronize it after the
# hook so logs follow the resolved user-data path too.
rex_app = Path("tools/rexglue/src/ui/rex_app.cpp")

old_user_dir_sync = """  PathConfig path_config{game_dir,  user_dir,     update_dir,
                         cache_dir, metadata_dir, exe_dir / (std::string(GetName()) + ".toml")};
  OnConfigurePaths(path_config);
  game_data_root_ = path_config.game_data_root;"""

new_user_dir_sync = """  PathConfig path_config{game_dir,  user_dir,     update_dir,
                         cache_dir, metadata_dir, exe_dir / (std::string(GetName()) + ".toml")};
  OnConfigurePaths(path_config);
  // OnConfigurePaths may redirect user data (for example to a portable folder).
  // Keep the local logging default in sync with the resolved path.
  user_dir = path_config.user_data_root;
  game_data_root_ = path_config.game_data_root;"""

patch_once(rex_app, old_user_dir_sync, new_user_dir_sync,
           "Portable UserData logging path")


# 4) VFS device selection and opens: specific nested mounts must beat the
# generic Partition1 mount, and an aliased directory itself must be resolvable
# even when OpenFile would otherwise split the path into parent + child.
old_device_lookup = """  // Find the device.
  auto it = std::find_if(devices_.cbegin(), devices_.cend(), [&](const auto& d) {
    return rex::string::utf8_starts_with_case(normalized_path, d->mount_path());
  });
  if (it == devices_.cend()) {"""

new_device_lookup = """  // Find the most specific device. Multiple mounts may overlap (for example
  // Partition1, BCGame and BCGame\\CookedXbox360), so longest-prefix wins.
  auto it = devices_.cend();
  size_t best_mount_length = 0;
  for (auto candidate = devices_.cbegin(); candidate != devices_.cend(); ++candidate) {
    const auto& mount_path = (*candidate)->mount_path();
    if (mount_path.size() > best_mount_length &&
        rex::string::utf8_starts_with_case(normalized_path, mount_path)) {
      it = candidate;
      best_mount_length = mount_path.size();
    }
  }
  if (it == devices_.cend()) {"""

patch_once(vfs, old_device_lookup, new_device_lookup,
           "VFS longest-prefix device selection")

old_open_lookup = """  // Lookup host device/parent path.
  // If no device or parent, fail.
  Entry* parent_entry = nullptr;
  Entry* entry = nullptr;

  auto base_path = rex::string::utf8_find_base_guest_path(path);
  if (!base_path.empty()) {
    parent_entry = !root_entry ? ResolvePath(base_path) : root_entry->ResolvePath(base_path);
    if (!parent_entry) {
      *out_action = FileAction::kDoesNotExist;
      return X_STATUS_NO_SUCH_FILE;
    }

    auto file_name = rex::string::utf8_find_name_from_guest_path(path);
    entry = parent_entry->GetChild(file_name);
  } else {
    entry = !root_entry ? ResolvePath(path) : root_entry->GetChild(path);
  }"""

new_open_lookup = """  // Lookup host device/parent path.
  // First resolve the complete path so nested VFS mounts can represent a
  // directory that doesn't physically exist in the generic parent device.
  Entry* parent_entry = nullptr;
  Entry* entry = nullptr;

  if (!root_entry) {
    entry = ResolvePath(path);
  } else {
    const bool looks_absolute =
        path.find(':') != std::string_view::npos ||
        rex::string::utf8_starts_with(path, "\\\\");
    if (!looks_absolute) {
      auto rooted_path =
          rex::string::utf8_join_guest_paths(root_entry->absolute_path(), path);
      entry = ResolvePath(rooted_path);
    }
  }
  if (entry) {
    parent_entry = entry->parent();
  }

  if (!entry) {
    auto base_path = rex::string::utf8_find_base_guest_path(path);
    if (!base_path.empty()) {
      parent_entry = !root_entry ? ResolvePath(base_path)
                                 : root_entry->ResolvePath(base_path);
      if (!parent_entry) {
        *out_action = FileAction::kDoesNotExist;
        return X_STATUS_NO_SUCH_FILE;
      }

      auto file_name = rex::string::utf8_find_name_from_guest_path(path);
      entry = parent_entry->GetChild(file_name);
    } else {
      entry = !root_entry ? ResolvePath(path) : root_entry->GetChild(path);
    }
  }"""

patch_once(vfs, old_open_lookup, new_open_lookup,
           "VFS full-path and relative mounted opens")
