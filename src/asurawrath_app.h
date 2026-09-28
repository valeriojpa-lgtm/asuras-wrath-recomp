#pragma once

#include <array>
#include <cstring>
#include <fstream>
#include <span>
#include <vector>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/disc_image_entry.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system.h>

#if defined(__ANDROID__)
#include <SDL3/SDL.h>
#include <SDL3/SDL_hints.h>
#include <android/log.h>
#endif

namespace {
bool ExtractDiscEntry(rex::filesystem::Entry *entry,
                      const std::filesystem::path &target_path) {
  std::error_code ec;
  if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
    std::filesystem::create_directories(target_path, ec);
    for (const auto &child : entry->children()) {
      auto child_target = target_path / child->name();
      if (!ExtractDiscEntry(child.get(), child_target)) {
        return false;
      }
    }
    return true;
  }

  auto parent_dir = target_path.parent_path();
  if (!parent_dir.empty()) {
    std::filesystem::create_directories(parent_dir, ec);
  }

  auto disc_entry = static_cast<rex::filesystem::DiscImageEntry *>(entry);
  if (!disc_entry || !disc_entry->mmap()) {
    return false;
  }

  std::ofstream out(target_path, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }

  const char *data_ptr = reinterpret_cast<const char *>(
      disc_entry->mmap()->data() + disc_entry->data_offset());
  size_t data_size = disc_entry->data_size();
  if (data_size > 0) {
    out.write(data_ptr, data_size);
  }
  out.close();
  return out.good();
}

bool ExtractIsoToDirectory(const std::filesystem::path &iso_path,
                           const std::filesystem::path &dest_dir) {
  bool success = false;
  std::error_code ec;
  std::filesystem::create_directories(dest_dir, ec);
  REXLOG_INFO("Extracting ISO {} to {}...", iso_path.string(),
              dest_dir.string());

  {
    rex::filesystem::DiscImageDevice device("game:", iso_path);
    if (!device.Initialize()) {
      REXLOG_ERROR(
          "Failed to initialize DiscImageDevice for ISO extraction: {}",
          iso_path.string());
      return false;
    }

    const rex::filesystem::Entry *root = device.root();
    if (!root) {
      REXLOG_ERROR("ISO disc image root entry is null");
      return false;
    }

    success = true;
    for (const auto &child : root->children()) {
      auto child_target = dest_dir / child->name();
      if (!ExtractDiscEntry(child.get(), child_target)) {
        REXLOG_ERROR("Failed to extract entry {} from ISO", child->name());
        success = false;
        break;
      }
    }
  }

  if (success) {
    REXLOG_INFO("Successfully extracted ISO to {}", dest_dir.string());
  }
  return success;
}

// --------------------------------------------------------------------------
// TU01 runtime image preparation.
//
// TU01 generated C++ must run against a TU01-patched guest image. ReXGlue
// already knows how to apply a sibling .xexp to a .xex. For preservation we
// keep the base XEX untouched and derive Data/Update/default.xexp from the
// user's original TU STFS package. A small ReXGlue VFS patch makes update:
// the fallback location for this XEXP.
// --------------------------------------------------------------------------
constexpr uint32_t kAsuraTitleId = 0x43430817;
constexpr size_t kTu01XexpSize = 573440;
constexpr uint32_t kTu01SourceVersion = 0x00000005;
constexpr uint32_t kTu01TargetVersion = 0x00000105;
constexpr std::array<uint8_t, 20> kTu01DigestSource = {
    0x09, 0x2B, 0xD0, 0x39, 0xE8, 0xD0, 0xD9, 0xF8, 0x68, 0xD4,
    0x8F, 0xE8, 0x8C, 0x0A, 0x68, 0x54, 0x07, 0xAD, 0xD8, 0x01};

uint32_t ReadBe32(const uint8_t *p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool ReadHostFile(const std::filesystem::path &path,
                  std::vector<uint8_t> &out) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec || size == 0 || size > (16ull * 1024ull * 1024ull)) {
    return false;
  }
  out.resize(static_cast<size_t>(size));
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    out.clear();
    return false;
  }
  in.read(reinterpret_cast<char *>(out.data()),
          static_cast<std::streamsize>(out.size()));
  if (!in || static_cast<size_t>(in.gcount()) != out.size()) {
    out.clear();
    return false;
  }
  return true;
}

bool ValidateTu01Xexp(std::span<const uint8_t> bytes) {
  if (bytes.size() != kTu01XexpSize || bytes.size() < 0x40) {
    return false;
  }
  if (std::memcmp(bytes.data(), "XEX2", 4) != 0) {
    return false;
  }

  const uint32_t header_count = ReadBe32(bytes.data() + 0x14);
  if (header_count > 128 ||
      0x18ull + uint64_t(header_count) * 8ull > bytes.size()) {
    return false;
  }

  constexpr uint32_t kDeltaPatchDescriptor = 0x000005FF;
  for (uint32_t i = 0; i < header_count; ++i) {
    const size_t opt = 0x18 + size_t(i) * 8;
    if (ReadBe32(bytes.data() + opt) != kDeltaPatchDescriptor) {
      continue;
    }

    const uint32_t desc = ReadBe32(bytes.data() + opt + 4);
    if (uint64_t(desc) + 0x4C > bytes.size()) {
      return false;
    }

    const uint32_t target_version = ReadBe32(bytes.data() + desc + 0x04);
    const uint32_t source_version = ReadBe32(bytes.data() + desc + 0x08);
    if (target_version != kTu01TargetVersion ||
        source_version != kTu01SourceVersion) {
      return false;
    }

    return std::memcmp(bytes.data() + desc + 0x0C,
                       kTu01DigestSource.data(),
                       kTu01DigestSource.size()) == 0;
  }
  return false;
}

rex::filesystem::Entry *
FindEntryNamed(rex::filesystem::Entry *entry, std::string_view wanted) {
  if (!entry) {
    return nullptr;
  }

  auto ascii_equal_ci = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
      return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
      auto ca = static_cast<unsigned char>(a[i]);
      auto cb = static_cast<unsigned char>(b[i]);
      if (ca >= 'A' && ca <= 'Z') ca = static_cast<unsigned char>(ca + 32);
      if (cb >= 'A' && cb <= 'Z') cb = static_cast<unsigned char>(cb + 32);
      if (ca != cb) return false;
    }
    return true;
  };

  if (ascii_equal_ci(entry->name(), wanted)) {
    return entry;
  }
  for (const auto &child : entry->children()) {
    if (auto *found = FindEntryNamed(child.get(), wanted)) {
      return found;
    }
  }
  return nullptr;
}

bool ExtractTu01Xexp(const std::filesystem::path &package_path,
                     std::vector<uint8_t> &xexp) {
  auto header =
      rex::filesystem::StfsContainerDevice::ReadPackageHeader(package_path);
  if (!header ||
      static_cast<uint32_t>(header->metadata.execution_info.title_id) !=
          kAsuraTitleId) {
    return false;
  }

  rex::filesystem::StfsContainerDevice device("tu:", package_path);
  if (!device.Initialize()) {
    return false;
  }

  auto *root = device.ResolvePath("");
  auto *entry = FindEntryNamed(root, "default.xexp");
  if (!entry || entry->size() != kTu01XexpSize) {
    return false;
  }

  rex::filesystem::File *file = nullptr;
  const auto status =
      entry->Open(rex::filesystem::FileAccess::kGenericRead, &file);
  if (XFAILED(status) || !file) {
    return false;
  }

  xexp.resize(entry->size());
  size_t bytes_read = 0;
  const auto read_status =
      file->ReadSync(std::span<uint8_t>(xexp), 0, &bytes_read);
  file->Destroy();

  if (XFAILED(read_status) || bytes_read != xexp.size()) {
    xexp.clear();
    return false;
  }
  return ValidateTu01Xexp(xexp);
}

bool WriteTu01XexpAtomically(const std::filesystem::path &target,
                             std::span<const uint8_t> bytes) {
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) {
    return false;
  }

  auto temp = target;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out.good()) {
      std::filesystem::remove(temp, ec);
      return false;
    }
  }

  // update/default.xexp is a derived cache file, never the preserved base XEX.
  if (std::filesystem::exists(target, ec)) {
    auto rejected = target;
    rejected += ".rejected";
    std::filesystem::remove(rejected, ec);
    ec.clear();
    std::filesystem::rename(target, rejected, ec);
    if (ec) {
      std::filesystem::remove(temp, ec);
      return false;
    }
  }

  ec.clear();
  std::filesystem::rename(temp, target, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

bool PrepareTu01RuntimePatch(const std::filesystem::path &update_dir) {
  std::error_code ec;
  std::filesystem::create_directories(update_dir, ec);
  if (ec) {
    REXLOG_ERROR("TU01: cannot create update directory {}: {}",
                 update_dir.string(), ec.message());
    return false;
  }

  const auto target = update_dir / "default.xexp";
  std::vector<uint8_t> existing;
  if (ReadHostFile(target, existing) && ValidateTu01Xexp(existing)) {
    REXLOG_INFO(
        "TU01 runtime patch ready: {} (0.0.0.5 -> 0.0.1.5)",
        target.string());
    return true;
  }

  if (std::filesystem::exists(target, ec)) {
    REXLOG_WARN("TU01: existing default.xexp is not the verified TU01 patch");
  }

  for (const auto &item : std::filesystem::directory_iterator(update_dir, ec)) {
    if (ec) {
      break;
    }
    if (!item.is_regular_file(ec) || item.path() == target) {
      continue;
    }

    std::vector<uint8_t> xexp;
    if (!ExtractTu01Xexp(item.path(), xexp)) {
      continue;
    }

    if (!WriteTu01XexpAtomically(target, xexp)) {
      REXLOG_ERROR("TU01: failed to write verified runtime XEXP to {}",
                   target.string());
      return false;
    }

    REXLOG_INFO("TU01: extracted verified default.xexp from {}",
                item.path().filename().string());
    REXLOG_INFO(
        "TU01 runtime patch ready: {} (0.0.0.5 -> 0.0.1.5)",
        target.string());
    return true;
  }

  REXLOG_ERROR(
      "TU01 runtime patch missing. Put the original Asura's Wrath TU01 STFS "
      "package in {}. The TU01 recomp will not boot against a retail image.",
      update_dir.string());
  return false;
}
} // namespace

// Global handle referenced by JNI
#if defined(__ANDROID__)
extern SDL_JoystickID g_VirtualJoystickID;
extern SDL_Gamepad *g_VirtualGamepad;
#endif

class AsurawrathApp : public rex::ReXApp {
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp>
  Create(rex::ui::WindowedAppContext &ctx) {
    return std::unique_ptr<AsurawrathApp>(
        new AsurawrathApp(ctx, "asura_wrath_recomp", PPCImageConfig));
  }

#if defined(__ANDROID__)
  void SetupVirtualGamepad() {
    if (g_VirtualJoystickID != 0)
      return;

    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) {
      SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    }

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 4;
    desc.nbuttons = 15;
    desc.name = "Virtual Touch Gamepad";

    g_VirtualJoystickID = SDL_AttachVirtualJoystick(&desc);

    if (g_VirtualJoystickID != 0) {
      SDL_GUID guid = SDL_GetJoystickGUIDForID(g_VirtualJoystickID);
      char guidStr[64];
      SDL_GUIDToString(guid, guidStr, sizeof(guidStr));

      char mapping[512];
      SDL_snprintf(
          mapping, sizeof(mapping),
          "%s,Virtual Touch "
          "Gamepad,a:b0,b:b1,x:b2,y:b3,back:b7,start:b6,leftshoulder:b4,"
          "rightshoulder:b5,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:"
          "a0,lefty:a1,rightx:a2,righty:a3,platform:Android,",
          guidStr);

      int res = SDL_AddGamepadMapping(mapping);
      g_VirtualGamepad = SDL_OpenGamepad(g_VirtualJoystickID);

      __android_log_print(
          ANDROID_LOG_INFO, "AsuraInput",
          "Native Virtual Gamepad Created (ID: %d, Mapping res: %d)",
          (int)g_VirtualJoystickID, res);
    } else {
      __android_log_print(ANDROID_LOG_ERROR, "AsuraInput",
                          "Failed to register Virtual Gamepad: %s",
                          SDL_GetError());
    }
  }
#endif

  void OnPreSetup(rex::RuntimeConfig &config) override {
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY, "1");
#endif
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
  }

  void SetupPcDataLayoutAliases() {
    auto *rt = runtime();
    if (!rt || !rt->file_system()) {
      return;
    }

    auto *vfs = rt->file_system();
    const auto data_root = game_data_root();
    const auto content_dir = data_root / "Game" / "Content";
    const auto cinematics_dir = data_root / "Game" / "Cinematics";

    std::error_code ec;
    const bool has_content = std::filesystem::is_directory(content_dir, ec);
    ec.clear();
    const bool has_cinematics =
        std::filesystem::is_directory(cinematics_dir, ec);

    // Legacy/extracted layouts do not need the PC compatibility mounts.
    if (!has_content && !has_cinematics) {
      return;
    }

    // UE3 expects a BCGame root containing Xbox360TOC.txt, CookedXbox360 and
    // Movies. Keep that guest view while storing the real data under clean PC
    // names. The lightweight compatibility root only contains the TOC and
    // placeholder directory names; the heavy directories are mounted directly
    // below their original guest paths.
    const auto compat_bcgame = user_data_root() / "vfs" / "BCGame";
    ec.clear();
    std::filesystem::create_directories(compat_bcgame / "CookedXbox360", ec);
    ec.clear();
    std::filesystem::create_directories(compat_bcgame / "Movies", ec);

    const std::filesystem::path toc_candidates[] = {
        data_root / "Game" / "Xbox360TOC.txt",
        data_root / "Xbox360TOC.txt",
        data_root / "BCGame" / "Xbox360TOC.txt",
        data_root.parent_path() / "BCGame" / "Xbox360TOC.txt",
    };

    std::filesystem::path toc_source;
    for (const auto &candidate : toc_candidates) {
      ec.clear();
      if (std::filesystem::is_regular_file(candidate, ec)) {
        toc_source = candidate;
        break;
      }
    }

    const auto toc_compat = compat_bcgame / "Xbox360TOC.txt";
    if (!toc_source.empty()) {
      ec.clear();
      std::filesystem::copy_file(
          toc_source, toc_compat,
          std::filesystem::copy_options::overwrite_existing, ec);
      if (ec) {
        REXLOG_ERROR("Failed to mirror Xbox360TOC.txt into compatibility VFS: {}",
                     ec.message());
      } else {
        REXLOG_INFO("Xbox360TOC compatibility source: {}",
                    toc_source.string());
      }
    } else {
      REXLOG_WARN(
          "Xbox360TOC.txt was not found in Data/Game, Data, or legacy BCGame");
    }

    auto register_mount = [&](const std::filesystem::path &host_dir,
                              std::string_view guest_mount) -> bool {
      std::error_code mount_ec;
      if (!std::filesystem::is_directory(host_dir, mount_ec)) {
        REXLOG_ERROR("PC data mount source directory missing: {}",
                     host_dir.string());
        return false;
      }

      auto device = std::make_unique<rex::filesystem::HostPathDevice>(
          guest_mount, host_dir, true);
      if (!device->Initialize()) {
        REXLOG_ERROR("Failed to initialize PC data mount {} -> {}",
                     guest_mount, host_dir.string());
        return false;
      }
      if (!vfs->RegisterDevice(std::move(device))) {
        REXLOG_ERROR("Failed to register PC data mount {}", guest_mount);
        return false;
      }

      REXLOG_INFO("PC data mount: {} -> {}", guest_mount, host_dir.string());
      return true;
    };

    register_mount(
        compat_bcgame,
        "\\Device\\Harddisk0\\Partition1\\BCGame");
    register_mount(
        content_dir,
        "\\Device\\Harddisk0\\Partition1\\BCGame\\CookedXbox360");
    register_mount(
        cinematics_dir,
        "\\Device\\Harddisk0\\Partition1\\BCGame\\Movies");
  }

  void OnPostSetup() override {
    SetupPcDataLayoutAliases();
#if defined(__ANDROID__)
    SetupVirtualGamepad();
#endif
  }

  bool ConstructRuntime(const rex::PathConfig &paths) override {
#if defined(_WIN32) && !defined(__ANDROID__)
    if (!PrepareTu01RuntimePatch(paths.update_data_root)) {
      const std::string msg =
          "TU01 runtime patch is missing or invalid.\n\n"
          "Put your original Asura's Wrath TU01 package in Data\\Update and "
          "start the game again.\n\n"
          "The preserved default.xex is never modified.";
      REXLOG_ERROR("{}", msg);
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, msg);
      return false;
    }
#endif
    return rex::ReXApp::ConstructRuntime(paths);
  }

  void OnConfigurePaths(rex::PathConfig &paths) override {
    std::error_code ec;

#if defined(_WIN32) && !defined(__ANDROID__)
    // Portable Windows layout by default. An explicit --user_data_root still
    // wins, which keeps the normal ReXGlue override available for power users.
    if (REXCVAR_GET(user_data_root).empty()) {
      paths.user_data_root =
          rex::filesystem::GetExecutableFolder() / "UserData";
    }
    if (REXCVAR_GET(update_data_root).empty()) {
      paths.update_data_root =
          rex::filesystem::GetExecutableFolder() / "Data" / "Update";
    }
#endif

    std::filesystem::create_directories(paths.user_data_root, ec);
    auto cache_dir = paths.user_data_root / "cache";
    ec.clear();
    std::filesystem::create_directories(cache_dir, ec);
    paths.cache_root = cache_dir;

    auto resolve_game_dir = [](const std::filesystem::path &dir,
                               std::filesystem::path &out_path) -> bool {
      std::error_code ec;
      if (dir.empty() || !std::filesystem::exists(dir, ec)) {
        return false;
      }
      if (std::filesystem::is_regular_file(dir, ec)) {
        auto ext = dir.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".iso" || ext == ".gdfx") {
          out_path = dir;
          return true;
        }
      }
      if (std::filesystem::exists(dir / "default.xex", ec)) {
        out_path = dir;
        return true;
      }
      if (std::filesystem::exists(dir / "Data" / "default.xex", ec)) {
        out_path = dir / "Data";
        return true;
      }
      if (std::filesystem::exists(dir / "BCGame" / "default.xex", ec)) {
        out_path = dir / "BCGame";
        return true;
      }
      if (std::filesystem::exists(dir / "extracted", ec)) {
        auto extracted_dir = dir / "extracted";
        if (std::filesystem::exists(extracted_dir / "default.xex", ec)) {
          out_path = extracted_dir;
          return true;
        }
        if (std::filesystem::exists(extracted_dir / "BCGame" / "default.xex",
                                    ec)) {
          out_path = extracted_dir / "BCGame";
          return true;
        }
        std::filesystem::directory_iterator end_it;
        for (std::filesystem::directory_iterator entry(extracted_dir, ec);
             entry != end_it && !ec; entry.increment(ec)) {
          if (entry->is_regular_file(ec)) {
            auto ext = entry->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".iso" || ext == ".gdfx") {
              out_path = entry->path();
              return true;
            }
          }
        }
        out_path = extracted_dir;
        return true;
      }
      if (std::filesystem::exists(dir / "game_data", ec)) {
        out_path = dir / "game_data";
        return true;
      }
      std::filesystem::directory_iterator end_it;
      for (std::filesystem::directory_iterator entry(dir, ec);
           entry != end_it && !ec; entry.increment(ec)) {
        if (entry->is_regular_file(ec)) {
          auto ext = entry->path().extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          if (ext == ".iso" || ext == ".gdfx") {
            out_path = entry->path();
            return true;
          }
        }
      }
      return false;
    };

    std::filesystem::path resolved;
    if (!paths.game_data_root.empty() &&
        resolve_game_dir(paths.game_data_root, resolved)) {
      paths.game_data_root = resolved;
    } else {
      auto cwd = std::filesystem::current_path(ec);
      const std::filesystem::path search_dirs[] = {
          paths.user_data_root, paths.user_data_root.parent_path(), cwd};

      for (const auto &dir : search_dirs) {
        if (resolve_game_dir(dir, resolved)) {
          paths.game_data_root = resolved;
          break;
        }
      }
    }

    auto cwd = std::filesystem::current_path(ec);
    if (paths.game_data_root.empty()) {
      paths.game_data_root =
          paths.user_data_root.empty() ? cwd : paths.user_data_root;
    }

    if (std::filesystem::is_regular_file(paths.game_data_root, ec)) {
      auto ext = paths.game_data_root.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".iso" || ext == ".gdfx") {
        auto iso_path = paths.game_data_root;
        auto dest_dir = paths.user_data_root.empty()
                            ? iso_path.parent_path() / "extracted"
                            : paths.user_data_root / "extracted";
        if (!std::filesystem::exists(dest_dir / "default.xex", ec) &&
            !std::filesystem::exists(dest_dir / "BCGame" / "default.xex", ec)) {
          if (ExtractIsoToDirectory(iso_path, dest_dir)) {
            std::filesystem::remove(iso_path, ec);
            std::filesystem::remove(paths.user_data_root / "Asura's Wrath.iso",
                                    ec);
            std::filesystem::remove(dest_dir / "Asura's Wrath.iso", ec);
            std::filesystem::path new_resolved;
            if (resolve_game_dir(dest_dir, new_resolved)) {
              paths.game_data_root = new_resolved;
            } else {
              paths.game_data_root = dest_dir;
            }
          }
        } else {
          std::filesystem::remove(iso_path, ec);
          std::filesystem::remove(paths.user_data_root / "Asura's Wrath.iso",
                                  ec);
          std::filesystem::remove(dest_dir / "Asura's Wrath.iso", ec);
          std::filesystem::path new_resolved;
          if (resolve_game_dir(dest_dir, new_resolved)) {
            paths.game_data_root = new_resolved;
          } else {
            paths.game_data_root = dest_dir;
          }
        }
      }
    }
  }
};
