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


# 5) Native Asura launcher: run the project-owned Win32 settings dialog before
# cvar::Init so graphics, input and language startup options are active before
# ReXGlue creates the runtime.
windowed_main = Path("tools/rexglue/src/ui/windowed_app_main_sdl.cpp")

old_launcher_include = """#include <rex/platform.h>
#include <rex/ui/windowed_app.h>"""

new_launcher_include = """#include <rex/platform.h>
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)
#include "asura_launcher_win.h"
#endif
#include <rex/ui/windowed_app.h>"""

patch_once(windowed_main, old_launcher_include, new_launcher_include,
           "Native launcher include")

old_main_entry = """int main(int argc, char* argv[]) {
  return RunWindowedApp(argc, argv);
}"""

new_main_entry = """int main(int argc, char* argv[]) {
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i] ? argv[i] : "");
  }
  if (!asura::RunNativeLauncher(args)) {
    return EXIT_SUCCESS;
  }
  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(args.size());
  for (auto& arg : args) {
    argv_ptrs.push_back(arg.data());
  }
  return RunWindowedApp(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
#else
  return RunWindowedApp(argc, argv);
#endif
}"""

patch_once(windowed_main, old_main_entry, new_main_entry,
           "Native launcher console entry")

old_wmain_launch = """  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

  std::vector<char*> argv_ptrs;"""

new_wmain_launch = """  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

#if defined(ASURA_NATIVE_LAUNCHER)
  if (!asura::RunNativeLauncher(utf8_args)) {
    return EXIT_SUCCESS;
  }
#endif

  std::vector<char*> argv_ptrs;"""

patch_once(windowed_main, old_wmain_launch, new_wmain_launch,
           "Native launcher Windows entry")


# 6) Asura queries the Xbox 360 language through XGetLanguage. ReXGlue's
# implementation currently hard-codes English even though user_language is an
# existing cvar. Make XGetLanguage honor that cvar so the native launcher can
# select the game's language before boot.
xam_info = Path("tools/rexglue/src/kernel/xam/xam_info.cpp")

old_xam_cvar_include = """#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>"""

new_xam_cvar_include = """#include <rex/cvar.h>
#include <rex/kernel/xam/module.h>
#include <rex/kernel/xam/private.h>"""

patch_once(xam_info, old_xam_cvar_include, new_xam_cvar_include,
           "XGetLanguage cvar include")

old_xam_namespace = """namespace rex {
namespace kernel {
namespace xam {"""

new_xam_namespace = """REXCVAR_DECLARE(uint32_t, user_language);

namespace rex {
namespace kernel {
namespace xam {"""

patch_once(xam_info, old_xam_namespace, new_xam_namespace,
           "XGetLanguage user_language declaration")

old_xgetlanguage = """u32 XGetLanguage_entry() {
  auto desired_language = XLanguage::kEnglish;

  // Switch the language based on game region.
  // TODO(benvanik): pull from xex header.
  uint32_t game_region = XEX_REGION_NTSCU;
  if (game_region & XEX_REGION_NTSCU) {
    desired_language = XLanguage::kEnglish;
  } else if (game_region & XEX_REGION_NTSCJ) {
    desired_language = XLanguage::kJapanese;
  }
  // Add more overrides?

  return uint32_t(desired_language);
}"""

new_xgetlanguage = """u32 XGetLanguage_entry() {
  uint32_t desired_language = REXCVAR_GET(user_language);
  // Xbox 360 dashboard language IDs used by XGetLanguage. ReXGlue's
  // user_language defaults to English (1); keep that as the fallback for
  // invalid / unsupported values.
  if (desired_language < 1 || desired_language > 17 || desired_language == 10) {
    desired_language = uint32_t(XLanguage::kEnglish);
  }
  REXKRNL_IMPORT_RESULT("XGetLanguage", "{}", desired_language);
  return desired_language;
}"""

patch_once(xam_info, old_xgetlanguage, new_xgetlanguage,
           "XGetLanguage user language support")


# 7) Theseus T04 save/profile bridge. Physical save policy is owned by the
# project-side NativeSaveSystem; ReXGlue keeps only the temporary Xbox XAM ABI.
runtime_cpp = Path("tools/rexglue/src/system/runtime.cpp")
old_runtime_save_cvars = """REXCVAR_DEFINE_STRING(game_data_root, "", "Runtime", "Override game data path");
REXCVAR_DEFINE_STRING(user_data_root, "", "Runtime", "Override user data path");
REXCVAR_DEFINE_STRING(update_data_root, "", "Runtime", "Override update data path");
REXCVAR_DEFINE_STRING(cache_root, "", "Runtime", "Override shader cache path");
REXCVAR_DEFINE_STRING(metadata_root, "", "Runtime", "Override metadata path");"""

new_runtime_save_cvars = """REXCVAR_DEFINE_STRING(game_data_root, "", "Runtime", "Override game data path");
REXCVAR_DEFINE_STRING(user_data_root, "", "Runtime", "Override user data path");
REXCVAR_DEFINE_STRING(update_data_root, "", "Runtime", "Override update data path");
REXCVAR_DEFINE_STRING(cache_root, "", "Runtime", "Override shader cache path");
REXCVAR_DEFINE_STRING(metadata_root, "", "Runtime", "Override metadata path");
REXCVAR_DEFINE_STRING(save_data_root, "", "Runtime", "Override XAM saved-game/profile root");
REXCVAR_DEFINE_STRING(user_profile_name, "User", "Runtime", "Compatibility profile name");
REXCVAR_DEFINE_STRING(user_profile_xuid, "", "Runtime", "Compatibility profile XUID");"""

patch_once(runtime_cpp, old_runtime_save_cvars, new_runtime_save_cvars,
           "T04 save/profile runtime cvars")

runtime_h = Path("tools/rexglue/include/rex/runtime.h")
old_runtime_save_decl = """REXCVAR_DECLARE(std::string, cache_root);
REXCVAR_DECLARE(std::string, metadata_root);"""

new_runtime_save_decl = """REXCVAR_DECLARE(std::string, cache_root);
REXCVAR_DECLARE(std::string, metadata_root);
REXCVAR_DECLARE(std::string, save_data_root);"""

patch_once(runtime_h, old_runtime_save_decl, new_runtime_save_decl,
           "T04 save root declaration")


# Keep Marketplace/DLC content on the existing ReXGlue content root, while
# moving only SavedGame packages and title-profile settings to Theseus Saves.
content_h = Path("tools/rexglue/include/rex/system/xam/content_manager.h")
old_content_ctor_h = """  ContentManager(KernelState* kernel_state, const std::filesystem::path& root_path);
  ~ContentManager();"""

new_content_ctor_h = """  ContentManager(KernelState* kernel_state, const std::filesystem::path& root_path,
                 const std::filesystem::path& save_root_path = {});
  ~ContentManager();"""

patch_once(content_h, old_content_ctor_h, new_content_ctor_h,
           "T04 ContentManager save-root constructor")

old_content_member_h = """  KernelState* kernel_state_;
  std::filesystem::path root_path_;

  // TODO(benvanik): remove use of global lock, it's bad here!"""

new_content_member_h = """  KernelState* kernel_state_;
  std::filesystem::path root_path_;
  std::filesystem::path save_root_path_;

  // TODO(benvanik): remove use of global lock, it's bad here!"""

patch_once(content_h, old_content_member_h, new_content_member_h,
           "T04 ContentManager save-root member")

content_cpp = Path("tools/rexglue/src/system/xam/content_manager.cpp")
old_content_ctor = """ContentManager::ContentManager(KernelState* kernel_state, const std::filesystem::path& root_path)
    : kernel_state_(kernel_state), root_path_(root_path) {}"""

new_content_ctor = """ContentManager::ContentManager(KernelState* kernel_state,
                               const std::filesystem::path& root_path,
                               const std::filesystem::path& save_root_path)
    : kernel_state_(kernel_state),
      root_path_(root_path),
      save_root_path_(save_root_path.empty() ? root_path : save_root_path) {}"""

patch_once(content_cpp, old_content_ctor, new_content_ctor,
           "T04 ContentManager native save root")

old_package_root = """  // Package root path:
  // content_root/xuid/title_id/content_type/
  return root_path_ / xuid_str / title_id_str / content_type_str;"""

new_package_root = """  // Saved games belong to the Theseus native save root. Marketplace/DLC and
  // other XAM content keep using the legacy content root until their own
  // migration milestone.
  const auto& base_root =
      content_type == XContentType::kSavedGame ? save_root_path_ : root_path_;

  // Package root path:
  // selected_root/xuid/title_id/content_type/
  return base_root / xuid_str / title_id_str / content_type_str;"""

patch_once(content_cpp, old_package_root, new_package_root,
           "T04 SavedGame package root")

old_header_root = """  // Header root path:
  // content_root/xuid/title_id/Headers/content_type/filename.header
  return root_path_ / xuid_str / title_id_str / kGameContentHeaderDirName / content_type_str /
         final_name;"""

new_header_root = """  const auto& base_root =
      content_type == XContentType::kSavedGame ? save_root_path_ : root_path_;

  // Header root path:
  // selected_root/xuid/title_id/Headers/content_type/filename.header
  return base_root / xuid_str / title_id_str / kGameContentHeaderDirName / content_type_str /
         final_name;"""

patch_once(content_cpp, old_header_root, new_header_root,
           "T04 SavedGame header root")

old_profile_root = """  // Per-game per-profile data location:
  // content_root/title_id/profile/user_name
  return root_path_ / title_id / kGameUserContentDirName / user_name;"""

new_profile_root = """  // Per-game per-profile data is save state too, so keep it under the native
  // Theseus Saves root alongside SavedGame packages.
  // save_root/title_id/profile/user_name
  return save_root_path_ / title_id / kGameUserContentDirName / user_name;"""

patch_once(content_cpp, old_profile_root, new_profile_root,
           "T04 title profile settings root")


kernel_state = Path("tools/rexglue/src/system/kernel_state.cpp")
old_content_manager_init = """  auto user_data_root = emulator_->user_data_root();
  if (!user_data_root.empty()) {
    user_data_root = std::filesystem::absolute(user_data_root);
  }
  content_manager_ = std::make_unique<xam::ContentManager>(this, user_data_root);"""

new_content_manager_init = """  auto user_data_root = emulator_->user_data_root();
  if (!user_data_root.empty()) {
    user_data_root = std::filesystem::absolute(user_data_root);
  }

  std::filesystem::path save_root;
  const auto save_root_arg = REXCVAR_GET(save_data_root);
  if (!save_root_arg.empty()) {
    save_root = rex::to_path(save_root_arg);
    save_root = std::filesystem::absolute(save_root);
  } else {
    save_root = user_data_root / "Saves";
  }

  content_manager_ =
      std::make_unique<xam::ContentManager>(this, user_data_root, save_root);"""

patch_once(kernel_state, old_content_manager_init, new_content_manager_init,
           "T04 ContentManager Theseus save root")


# Compatibility profile identity comes from Theseus. Defaults intentionally
# preserve the pre-T04 ReXGlue identity for existing save compatibility.
profile_cpp = Path("tools/rexglue/src/system/xam/user_profile.cpp")
old_profile_include = """#include <fmt/format.h>

#include <rex/logging.h>"""

new_profile_include = """#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>"""

patch_once(profile_cpp, old_profile_include, new_profile_include,
           "T04 profile cvar include")

old_profile_namespace = """namespace rex {
namespace system {
namespace xam {

UserProfile::UserProfile() {"""

new_profile_namespace = """REXCVAR_DECLARE(std::string, user_profile_name);
REXCVAR_DECLARE(std::string, user_profile_xuid);

namespace rex {
namespace system {
namespace xam {

UserProfile::UserProfile() {"""

patch_once(profile_cpp, old_profile_namespace, new_profile_namespace,
           "T04 profile cvar declarations")

old_profile_identity = """  xuid_ = 0xB13EBABEBABEBABE;
  name_ = "User";

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195"""

new_profile_identity = """  xuid_ = 0xB13EBABEBABEBABE;
  name_ = "User";

  const auto configured_name = REXCVAR_GET(user_profile_name);
  if (!configured_name.empty()) {
    name_ = configured_name;
  }

  const auto configured_xuid = REXCVAR_GET(user_profile_xuid);
  if (!configured_xuid.empty()) {
    try {
      xuid_ = std::stoull(configured_xuid, nullptr, 0);
    } catch (...) {
      REXSYS_WARN("Invalid user_profile_xuid '{}'; preserving compatibility XUID",
                  configured_xuid);
    }
  }

  // https://cs.rin.ru/forum/viewtopic.php?f=38&t=60668&hilit=gfwl+live&start=195"""

patch_once(profile_cpp, old_profile_identity, new_profile_identity,
           "T04 Theseus profile identity")


# Detect the precise first-run condition: the game enumerates SavedGame content
# and gets zero items. This flag is consumed only by the next 2-button message.
xam_content = Path("tools/rexglue/src/kernel/xam/xam_content.cpp")
old_content_atomic_include = """#include <rex/cvar.h>
#include <rex/kernel/xam/private.h>"""

new_content_atomic_include = """#include <atomic>

#include <rex/cvar.h>
#include <rex/kernel/xam/private.h>"""

patch_once(xam_content, old_content_atomic_include, new_content_atomic_include,
           "T04 first-run atomic include")

old_content_namespace = """namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;"""

new_content_namespace = """namespace rex {
namespace kernel {
namespace xam {
using namespace rex::system;
using namespace rex::system::xam;

std::atomic_bool g_asura_missing_saved_game{false};"""

patch_once(xam_content, old_content_namespace, new_content_namespace,
           "T04 first-run save state")

old_enumerator_tail = """  REXKRNL_DEBUG("XamContentCreateEnumerator: added {} items to enumerator", e->item_count());

  *handle_out = e->handle();"""

new_enumerator_tail = """  REXKRNL_DEBUG("XamContentCreateEnumerator: added {} items to enumerator", e->item_count());

  if (uint32_t(content_type) == uint32_t(XContentType::kSavedGame)) {
    g_asura_missing_saved_game.store(e->item_count() == 0,
                                     std::memory_order_release);
  }

  *handle_out = e->handle();"""

patch_once(xam_content, old_enumerator_tail, new_enumerator_tail,
           "T04 first-run SavedGame detection")


xam_ui = Path("tools/rexglue/src/kernel/xam/xam_ui.cpp")
old_ui_atomic_include = """#include <rex/logging.h>
#include <rex/runtime.h>"""

new_ui_atomic_include = """#include <atomic>

#include <rex/logging.h>
#include <rex/runtime.h>"""

patch_once(xam_ui, old_ui_atomic_include, new_ui_atomic_include,
           "T04 auto-save prompt atomic include")

old_headless_cvar = """REXCVAR_DEFINE_BOOL(headless, false, "Kernel",
                    "Don't display any UI, using defaults for prompts as needed");"""

new_headless_cvar = """REXCVAR_DEFINE_BOOL(headless, false, "Kernel",
                    "Don't display any UI, using defaults for prompts as needed");
REXCVAR_DEFINE_BOOL(asura_auto_first_save_prompt, true, "Theseus",
                    "Automatically confirm Asura's first-run save creation prompt");
REXCVAR_DEFINE_BOOL(asura_first_run, false, "Theseus",
                    "Theseus detected no prior Asura save/profile/options data");"""

patch_once(xam_ui, old_headless_cvar, new_headless_cvar,
           "T04 auto first-save prompt cvar")

old_ui_dialog_extern = """extern std::atomic<int> xam_dialogs_shown_;"""

new_ui_dialog_extern = """extern std::atomic<int> xam_dialogs_shown_;
extern std::atomic_bool g_asura_missing_saved_game;
static std::atomic_bool g_asura_first_run_prompt_consumed{false};"""

patch_once(xam_ui, old_ui_dialog_extern, new_ui_dialog_extern,
           "T04 first-run save extern")

old_message_result = """  X_RESULT result;
  if (REXCVAR_GET(headless)) {"""

new_message_result = """  // Project-specific PC polish. T04.1 receives an explicit first-run decision
  // from Theseus before XAM starts, so this no longer depends on SavedGame
  // enumeration ordering. Only a two-button prompt with the first button
  // focused is eligible, and the explicit first-run path is strictly one-shot.
  const bool explicit_first_run_candidate =
      REXCVAR_GET(asura_first_run) && button_count == 2 && active_button == 0;
  const bool explicit_first_run =
      explicit_first_run_candidate &&
      !g_asura_first_run_prompt_consumed.exchange(true, std::memory_order_acq_rel);

  // Keep the T04 enumeration signal as a compatibility fallback, but consume it
  // immediately so it cannot leak into an unrelated later dialog.
  const bool missing_saved_game =
      g_asura_missing_saved_game.exchange(false, std::memory_order_acq_rel);

  if (REXCVAR_GET(asura_auto_first_save_prompt) &&
      (explicit_first_run ||
       (missing_saved_game && button_count == 2 && active_button == 0))) {
    auto run = [result_ptr]() -> X_RESULT {
      *result_ptr = 0;  // first button: affirmative in Asura's creation prompt
      return X_ERROR_SUCCESS;
    };
    return xeXamDispatchHeadless(run, overlapped.guest_address());
  }

  X_RESULT result;
  if (REXCVAR_GET(headless)) {"""

patch_once(xam_ui, old_message_result, new_message_result,
           "T04 auto-confirm first save creation")


# 8) Theseus T05 input-policy bridge. The host input policy and bindings live
# in src/platform; ReXGlue temporarily supplies the physical SDL/XInput drivers
# and the Xbox XAM input ABI.
mnk_cpp = Path("tools/rexglue/src/input/mnk/mnk_input_driver.cpp")

old_t05_cursor_cvar = """REXCVAR_DEFINE_DOUBLE(mnk_sensitivity, 1.0, "Input", "Mouse sensitivity for right stick")
    .range(0.01, 10.0);"""

new_t05_cursor_cvar = """REXCVAR_DEFINE_DOUBLE(mnk_sensitivity, 1.0, "Input", "Mouse sensitivity for right stick")
    .range(0.01, 10.0);
REXCVAR_DEFINE_BOOL(theseus_hide_cursor_in_game, true, "Theseus",
                    "Hide the pointer while the Asura game window has focus");"""

patch_once(mnk_cpp, old_t05_cursor_cvar, new_t05_cursor_cvar,
           "T05 Theseus cursor policy cvar")

old_t05_window_attach = """    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }
}"""

new_t05_window_attach = """    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
    if (REXCVAR_GET(theseus_hide_cursor_in_game) && window->HasFocus()) {
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    }
  }
}"""

patch_once(mnk_cpp, old_t05_window_attach, new_t05_window_attach,
           "T05 hide cursor on focused game window")

old_t05_lost_focus = """  if (attached_window_) {
    ReleaseMouseCaptureFromUIThread(attached_window_);
  }
}

void MnkInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
}"""

new_t05_lost_focus = """  if (attached_window_) {
    ReleaseMouseCaptureFromUIThread(attached_window_);
    if (REXCVAR_GET(theseus_hide_cursor_in_game)) {
      attached_window_->SetCursorVisibility(
          rex::ui::Window::CursorVisibility::kVisible);
    }
  }
}

void MnkInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
  if (attached_window_ && REXCVAR_GET(theseus_hide_cursor_in_game)) {
    attached_window_->SetCursorVisibility(
        rex::ui::Window::CursorVisibility::kHidden);
  }
}"""

patch_once(mnk_cpp, old_t05_lost_focus, new_t05_lost_focus,
           "T05 restore cursor on focus loss")


# 9) Theseus T06 audio-policy bridge. Audio policy/configuration is owned by
# Theseus; XMA decode and SDL sample output remain compatibility bridges.
sdl_audio_cpp = Path("tools/rexglue/src/audio/sdl/sdl_audio_driver.cpp")

old_t06_audio_metadata = """  // Set app name for audio device identification
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "rexglue");"""

new_t06_audio_metadata = """  // T06: expose the actual game identity to the host audio stack rather than
  // leaking the compatibility runtime name into mixers/device diagnostics.
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING, "Asura's Wrath");"""

patch_once(sdl_audio_cpp, old_t06_audio_metadata, new_t06_audio_metadata,
           "T06 host audio app identity")


# 10) Theseus T07 native timing. ReXGlue keeps the Xbox kernel export ABI, but
# its public Clock surface delegates host/guest timing to project-owned code.
clock_cpp = Path("tools/rexglue/src/core/clock.cpp")

old_t07_clock_include = """#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/math.h>"""

new_t07_clock_include = """#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/math.h>

#include "compat/theseus_timing_bridge.h"
"""

patch_once(clock_cpp, old_t07_clock_include, new_t07_clock_include,
           "T07 timing bridge include")

old_t07_public_clock = """uint64_t Clock::QueryHostTickFrequency() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_frequency_raw();
  }
#endif
  return host_tick_frequency_platform();
}
uint64_t Clock::QueryHostTickCount() {
#if REX_CLOCK_RAW_AVAILABLE
  if (REXCVAR_GET(clock_source_raw)) {
    return host_tick_count_raw();
  }
#endif
  return host_tick_count_platform();
}

double Clock::guest_time_scalar() {
  return guest_time_scalar_;
}

void Clock::set_guest_time_scalar(double scalar) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }

  guest_time_scalar_ = scalar;
  RecomputeGuestTickScalar();
}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() {
  std::lock_guard<std::mutex> lock(tick_mutex_);
  return guest_tick_ratio_;
}

uint64_t Clock::guest_tick_frequency() {
  return guest_tick_frequency_;
}

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  guest_tick_frequency_ = frequency;
  RecomputeGuestTickScalar();
}

uint64_t Clock::guest_system_time_base() {
  return guest_system_time_base_;
}

void Clock::set_guest_system_time_base(uint64_t time_base) {
  guest_system_time_base_ = time_base;
}

uint64_t Clock::QueryGuestTickCount() {
  auto guest_tick_count = UpdateGuestClock();
  return guest_tick_count;
}

uint64_t Clock::QueryGuestSystemTime() {
  if (REXCVAR_GET(clock_no_scaling)) {
    return Clock::QueryHostSystemTime();
  }

  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  return guest_system_time_base_ + guest_system_time_offset;
}

uint32_t Clock::QueryGuestUptimeMillis() {
  return static_cast<uint32_t>(std::min<uint64_t>(QueryGuestSystemTimeOffset() / 10000,
                                                  std::numeric_limits<uint32_t>::max()));
}

void Clock::SetGuestSystemTime(uint64_t system_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    // Time is fixed to host time.
    return;
  }

  // Query the filetime offset to calculate a new base time.
  auto guest_system_time_offset = QueryGuestSystemTimeOffset();
  guest_system_time_base_ = system_time - guest_system_time_offset;
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return guest_ms;
  }

  constexpr uint64_t max = std::numeric_limits<uint32_t>::max();

  if (guest_ms >= max) {
    return max;
  } else if (!guest_ms) {
    return 0;
  }
  uint64_t scaled_ms =
      static_cast<uint64_t>((static_cast<uint64_t>(guest_ms) * guest_time_scalar_));
  return static_cast<uint32_t>(std::min(scaled_ms, max));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return static_cast<uint64_t>(guest_file_time);
  }

  if (!guest_file_time) {
    return 0;
  } else if (guest_file_time > 0) {
    // Absolute time.
    uint64_t guest_time = Clock::QueryGuestSystemTime();
    int64_t relative_time = guest_file_time - static_cast<int64_t>(guest_time);
    int64_t scaled_time = static_cast<int64_t>(relative_time * guest_time_scalar_);
    return static_cast<int64_t>(guest_time) + scaled_time;
  } else {
    // Relative time.
    uint64_t scaled_file_time =
        static_cast<uint64_t>((static_cast<uint64_t>(guest_file_time) * guest_time_scalar_));
    // TODO(benvanik): check for overflow?
    return scaled_file_time;
  }
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }

  uint64_t scaled_sec = static_cast<uint64_t>(static_cast<uint64_t>(*tv_sec) * guest_time_scalar_);
  uint64_t scaled_usec =
      static_cast<uint64_t>(static_cast<uint64_t>(*tv_usec) * guest_time_scalar_);
  if (scaled_usec > std::numeric_limits<uint32_t>::max()) {
    uint64_t overflow_sec = scaled_usec / 1000000;
    scaled_usec -= overflow_sec * 1000000;
    scaled_sec += overflow_sec;
  }
  *tv_sec = int32_t(scaled_sec);
  *tv_usec = int32_t(scaled_usec);
}"""

new_t07_public_clock = """uint64_t Clock::QueryHostTickFrequency() {
  return theseus::timing::HostTickFrequency();
}

uint64_t Clock::QueryHostTickCount() {
  return theseus::timing::HostTickCount();
}

double Clock::guest_time_scalar() {
  return theseus::timing::GuestTimeScalar();
}

void Clock::set_guest_time_scalar(double scalar) {
  if (REXCVAR_GET(clock_no_scaling)) {
    return;
  }
  theseus::timing::SetGuestTimeScalar(scalar);
}

std::pair<uint64_t, uint64_t> Clock::guest_tick_ratio() {
  return theseus::timing::GuestTickRatio();
}

uint64_t Clock::guest_tick_frequency() {
  return theseus::timing::GuestTickFrequency();
}

void Clock::set_guest_tick_frequency(uint64_t frequency) {
  theseus::timing::SetGuestTickFrequency(frequency);
}

uint64_t Clock::guest_system_time_base() {
  return theseus::timing::GuestSystemTimeBase();
}

void Clock::set_guest_system_time_base(uint64_t time_base) {
  theseus::timing::SetGuestSystemTimeBase(time_base);
}

uint64_t Clock::QueryGuestTickCount() {
  return theseus::timing::QueryGuestTickCount(REXCVAR_GET(clock_no_scaling));
}

uint64_t Clock::QueryGuestSystemTime() {
  return theseus::timing::QueryGuestSystemTime(REXCVAR_GET(clock_no_scaling));
}

uint32_t Clock::QueryGuestUptimeMillis() {
  return theseus::timing::QueryGuestUptimeMillis(REXCVAR_GET(clock_no_scaling));
}

void Clock::SetGuestSystemTime(uint64_t system_time) {
  theseus::timing::SetGuestSystemTime(system_time, REXCVAR_GET(clock_no_scaling));
}

uint32_t Clock::ScaleGuestDurationMillis(uint32_t guest_ms) {
  return theseus::timing::ScaleGuestDurationMillis(
      guest_ms, REXCVAR_GET(clock_no_scaling));
}

int64_t Clock::ScaleGuestDurationFileTime(int64_t guest_file_time) {
  return theseus::timing::ScaleGuestDurationFileTime(
      guest_file_time, REXCVAR_GET(clock_no_scaling));
}

void Clock::ScaleGuestDurationTimeval(int32_t* tv_sec, int32_t* tv_usec) {
  theseus::timing::ScaleGuestDurationTimeval(
      tv_sec, tv_usec, REXCVAR_GET(clock_no_scaling));
}"""

patch_once(clock_cpp, old_t07_public_clock, new_t07_public_clock,
           "T07 delegate ReXGlue clock surface to Theseus")


clock_win_cpp = Path("tools/rexglue/src/core/clock_win.cpp")

old_t07_clock_win_include = """#include <rex/chrono/clock.h>
#include <rex/platform.h>"""

new_t07_clock_win_include = """#include <rex/chrono/clock.h>
#include <rex/platform.h>

#include "compat/theseus_timing_bridge.h"
"""

patch_once(clock_win_cpp, old_t07_clock_win_include, new_t07_clock_win_include,
           "T07 Windows host clock bridge include")

old_t07_clock_win_impl = """uint64_t Clock::host_tick_frequency_platform() {
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);
  return frequency.QuadPart;
}

uint64_t Clock::host_tick_count_platform() {
  LARGE_INTEGER counter;
  uint64_t time = 0;
  if (QueryPerformanceCounter(&counter)) {
    time = counter.QuadPart;
  }
  return time;
}

uint64_t Clock::QueryHostSystemTime() {
  FILETIME t;
  GetSystemTimeAsFileTime(&t);
  return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}

uint64_t Clock::QueryHostUptimeMillis() {
  return host_tick_count_platform() * 1000 / host_tick_frequency_platform();
}"""

new_t07_clock_win_impl = """uint64_t Clock::host_tick_frequency_platform() {
  return theseus::timing::HostTickFrequency();
}

uint64_t Clock::host_tick_count_platform() {
  return theseus::timing::HostTickCount();
}

uint64_t Clock::QueryHostSystemTime() {
  return theseus::timing::HostSystemTime();
}

uint64_t Clock::QueryHostUptimeMillis() {
  return theseus::timing::HostUptimeMillis();
}"""

patch_once(clock_win_cpp, old_t07_clock_win_impl, new_t07_clock_win_impl,
           "T07 delegate Windows host clock to Theseus")


# 11) Theseus T08 host synchronization foundation. ReXGlue keeps the public
# rex::thread object interfaces and Xbox kernel ABI, but Win32 synchronization
# operations delegate to the project-owned Theseus bridge.
threading_win_cpp = Path("tools/rexglue/src/core/threading_win.cpp")

old_t08_sync_include = """#include <rex/assert.h>
#include <rex/chrono/chrono_steady_cast.h>"""

new_t08_sync_include = """#include <limits>

#include <rex/assert.h>
#include <rex/chrono/chrono_steady_cast.h>

#include "compat/theseus_sync_bridge.h"
"""

patch_once(threading_win_cpp, old_t08_sync_include, new_t08_sync_include,
           "T08 synchronization bridge include")

old_t08_basic_ops = """void MaybeYield() {
  SwitchToThread();
  MemoryBarrier();
}

void SyncMemory() {
  MemoryBarrier();
}

void Sleep(std::chrono::microseconds duration) {
  if (duration.count() < 100) {
    MaybeYield();
  } else {
    ::Sleep(static_cast<DWORD>(duration.count() / 1000));
  }
}

SleepResult AlertableSleep(std::chrono::microseconds duration) {
  if (SleepEx(static_cast<DWORD>(duration.count() / 1000), TRUE) == WAIT_IO_COMPLETION) {
    return SleepResult::kAlerted;
  }
  return SleepResult::kSuccess;
}

TlsHandle AllocateTlsHandle() {
  return TlsAlloc();
}

bool FreeTlsHandle(TlsHandle handle) {
  return TlsFree(handle) ? true : false;
}

uintptr_t GetTlsValue(TlsHandle handle) {
  return reinterpret_cast<uintptr_t>(TlsGetValue(handle));
}

bool SetTlsValue(TlsHandle handle, uintptr_t value) {
  return TlsSetValue(handle, reinterpret_cast<void*>(value)) ? true : false;
}"""

new_t08_basic_ops = """void MaybeYield() {
  theseus::sync::YieldHostThread();
}

void SyncMemory() {
  theseus::sync::FullMemoryFence();
}

void Sleep(std::chrono::microseconds duration) {
  theseus::sync::SleepMicros(
      static_cast<uint64_t>(std::max<int64_t>(0, duration.count())));
}

SleepResult AlertableSleep(std::chrono::microseconds duration) {
  return theseus::sync::AlertableSleepMicros(
             static_cast<uint64_t>(std::max<int64_t>(0, duration.count()))) ==
                 theseus::sync::AlertableSleepOutcome::kAlerted
             ? SleepResult::kAlerted
             : SleepResult::kSuccess;
}

TlsHandle AllocateTlsHandle() {
  return static_cast<TlsHandle>(theseus::sync::AllocateTls());
}

bool FreeTlsHandle(TlsHandle handle) {
  return theseus::sync::FreeTls(static_cast<uint32_t>(handle));
}

uintptr_t GetTlsValue(TlsHandle handle) {
  return theseus::sync::GetTls(static_cast<uint32_t>(handle));
}

bool SetTlsValue(TlsHandle handle, uintptr_t value) {
  return theseus::sync::SetTls(static_cast<uint32_t>(handle), value);
}"""

patch_once(threading_win_cpp, old_t08_basic_ops, new_t08_basic_ops,
           "T08 delegate yield sleep and TLS to Theseus")

old_t08_handle_dtor = """  ~Win32Handle() override {
    CloseHandle(handle_);
    handle_ = nullptr;
  }"""

new_t08_handle_dtor = """  ~Win32Handle() override {
    theseus::sync::CloseNativeHandle(handle_);
    handle_ = nullptr;
  }"""

patch_once(threading_win_cpp, old_t08_handle_dtor, new_t08_handle_dtor,
           "T08 delegate native handle lifetime to Theseus")

old_t08_waits = """WaitResult Wait(WaitHandle* wait_handle, bool is_alertable, std::chrono::milliseconds timeout) {
  HANDLE handle = wait_handle->native_handle();
  DWORD result = WaitForSingleObjectEx(handle, DWORD(timeout.count()), is_alertable ? TRUE : FALSE);
  switch (result) {
    case WAIT_OBJECT_0:
      return WaitResult::kSuccess;
    case WAIT_ABANDONED:
      return WaitResult::kAbandoned;
    case WAIT_IO_COMPLETION:
      return WaitResult::kUserCallback;
    case WAIT_TIMEOUT:
      return WaitResult::kTimeout;
    default:
    case WAIT_FAILED:
      return WaitResult::kFailed;
  }
}

WaitResult SignalAndWait(WaitHandle* wait_handle_to_signal, WaitHandle* wait_handle_to_wait_on,
                         bool is_alertable, std::chrono::milliseconds timeout) {
  HANDLE handle_to_signal = wait_handle_to_signal->native_handle();
  HANDLE handle_to_wait_on = wait_handle_to_wait_on->native_handle();
  DWORD result = SignalObjectAndWait(handle_to_signal, handle_to_wait_on, DWORD(timeout.count()),
                                     is_alertable ? TRUE : FALSE);
  switch (result) {
    case WAIT_OBJECT_0:
      return WaitResult::kSuccess;
    case WAIT_ABANDONED:
      return WaitResult::kAbandoned;
    case WAIT_IO_COMPLETION:
      return WaitResult::kUserCallback;
    case WAIT_TIMEOUT:
      return WaitResult::kTimeout;
    default:
    case WAIT_FAILED:
      return WaitResult::kFailed;
  }
}

std::pair<WaitResult, size_t> WaitMultiple(WaitHandle* wait_handles[], size_t wait_handle_count,
                                           bool wait_all, bool is_alertable,
                                           std::chrono::milliseconds timeout) {
  std::vector<HANDLE> handles(wait_handle_count);
  for (size_t i = 0; i < wait_handle_count; ++i) {
    handles[i] = wait_handles[i]->native_handle();
  }
  DWORD result =
      WaitForMultipleObjectsEx(DWORD(handles.size()), handles.data(), wait_all ? TRUE : FALSE,
                               DWORD(timeout.count()), is_alertable ? TRUE : FALSE);
  if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + handles.size()) {
    return std::pair<WaitResult, size_t>(WaitResult::kSuccess, result - WAIT_OBJECT_0);
  } else if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + handles.size()) {
    return std::pair<WaitResult, size_t>(WaitResult::kAbandoned, result - WAIT_ABANDONED_0);
  }
  switch (result) {
    case WAIT_IO_COMPLETION:
      return std::pair<WaitResult, size_t>(WaitResult::kUserCallback, 0);
    case WAIT_TIMEOUT:
      return std::pair<WaitResult, size_t>(WaitResult::kTimeout, 0);
    default:
    case WAIT_FAILED:
      return std::pair<WaitResult, size_t>(WaitResult::kFailed, 0);
  }
}"""

new_t08_waits = """static WaitResult FromTheseusWait(theseus::sync::WaitOutcome result) {
  switch (result) {
    case theseus::sync::WaitOutcome::kSuccess:
      return WaitResult::kSuccess;
    case theseus::sync::WaitOutcome::kUserCallback:
      return WaitResult::kUserCallback;
    case theseus::sync::WaitOutcome::kTimeout:
      return WaitResult::kTimeout;
    case theseus::sync::WaitOutcome::kAbandoned:
      return WaitResult::kAbandoned;
    default:
      return WaitResult::kFailed;
  }
}

static uint64_t ToTheseusTimeout(std::chrono::milliseconds timeout) {
  return timeout == std::chrono::milliseconds::max()
             ? std::numeric_limits<uint64_t>::max()
             : static_cast<uint64_t>(std::max<int64_t>(0, timeout.count()));
}

WaitResult Wait(WaitHandle* wait_handle, bool is_alertable, std::chrono::milliseconds timeout) {
  return FromTheseusWait(theseus::sync::WaitOne(
      wait_handle->native_handle(), is_alertable, ToTheseusTimeout(timeout)));
}

WaitResult SignalAndWait(WaitHandle* wait_handle_to_signal, WaitHandle* wait_handle_to_wait_on,
                         bool is_alertable, std::chrono::milliseconds timeout) {
  return FromTheseusWait(theseus::sync::SignalAndWait(
      wait_handle_to_signal->native_handle(), wait_handle_to_wait_on->native_handle(),
      is_alertable, ToTheseusTimeout(timeout)));
}

std::pair<WaitResult, size_t> WaitMultiple(WaitHandle* wait_handles[], size_t wait_handle_count,
                                           bool wait_all, bool is_alertable,
                                           std::chrono::milliseconds timeout) {
  std::vector<void*> handles(wait_handle_count);
  for (size_t i = 0; i < wait_handle_count; ++i) {
    handles[i] = wait_handles[i]->native_handle();
  }
  const auto result = theseus::sync::WaitMany(
      handles.data(), handles.size(), wait_all, is_alertable,
      ToTheseusTimeout(timeout));
  return {FromTheseusWait(result.outcome), result.index};
}"""

patch_once(threading_win_cpp, old_t08_waits, new_t08_waits,
           "T08 delegate waits to Theseus")

old_t08_events = """class Win32Event : public Win32Handle<Event> {
 public:
  explicit Win32Event(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Event() override = default;
  void Set() override { SetEvent(handle_); }
  void Reset() override { ResetEvent(handle_); }
  void Pulse() override { PulseEvent(handle_); }
};

std::unique_ptr<Event> Event::CreateManualResetEvent(bool initial_state) {
  HANDLE handle = CreateEvent(nullptr, TRUE, initial_state ? TRUE : FALSE, nullptr);
  if (handle) {
    return std::make_unique<Win32Event>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}

std::unique_ptr<Event> Event::CreateAutoResetEvent(bool initial_state) {
  HANDLE handle = CreateEvent(nullptr, FALSE, initial_state ? TRUE : FALSE, nullptr);
  if (handle) {
    return std::make_unique<Win32Event>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}"""

new_t08_events = """class Win32Event : public Win32Handle<Event> {
 public:
  explicit Win32Event(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Event() override = default;
  void Set() override { theseus::sync::SetEventSignaled(handle_); }
  void Reset() override { theseus::sync::ResetEventSignaled(handle_); }
  void Pulse() override { theseus::sync::PulseEventSignaled(handle_); }
};

std::unique_ptr<Event> Event::CreateManualResetEvent(bool initial_state) {
  auto handle = static_cast<HANDLE>(theseus::sync::CreateEventHandle(true, initial_state));
  if (handle) {
    return std::make_unique<Win32Event>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}

std::unique_ptr<Event> Event::CreateAutoResetEvent(bool initial_state) {
  auto handle = static_cast<HANDLE>(theseus::sync::CreateEventHandle(false, initial_state));
  if (handle) {
    return std::make_unique<Win32Event>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}"""

patch_once(threading_win_cpp, old_t08_events, new_t08_events,
           "T08 delegate events to Theseus")

old_t08_semaphore = """class Win32Semaphore : public Win32Handle<Semaphore> {
 public:
  explicit Win32Semaphore(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Semaphore() override = default;
  bool Release(int release_count, int* out_previous_count) override {
    return ReleaseSemaphore(handle_, release_count, reinterpret_cast<LPLONG>(out_previous_count))
               ? true
               : false;
  }
};

std::unique_ptr<Semaphore> Semaphore::Create(int initial_count, int maximum_count) {
  HANDLE handle = CreateSemaphore(nullptr, initial_count, maximum_count, nullptr);
  if (handle) {
    return std::make_unique<Win32Semaphore>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}"""

new_t08_semaphore = """class Win32Semaphore : public Win32Handle<Semaphore> {
 public:
  explicit Win32Semaphore(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Semaphore() override = default;
  bool Release(int release_count, int* out_previous_count) override {
    return theseus::sync::ReleaseSemaphore(
        handle_, release_count,
        reinterpret_cast<int32_t*>(out_previous_count));
  }
};

std::unique_ptr<Semaphore> Semaphore::Create(int initial_count, int maximum_count) {
  auto handle = static_cast<HANDLE>(
      theseus::sync::CreateSemaphoreHandle(initial_count, maximum_count));
  if (handle) {
    return std::make_unique<Win32Semaphore>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}"""

patch_once(threading_win_cpp, old_t08_semaphore, new_t08_semaphore,
           "T08 delegate semaphores to Theseus")

old_t08_mutant = """class Win32Mutant : public Win32Handle<Mutant> {
 public:
  explicit Win32Mutant(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Mutant() = default;
  bool Release() override { return ReleaseMutex(handle_) ? true : false; }
};

std::unique_ptr<Mutant> Mutant::Create(bool initial_owner) {
  HANDLE handle = CreateMutex(nullptr, initial_owner ? TRUE : FALSE, nullptr);
  if (handle) {
    return std::make_unique<Win32Mutant>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}"""

new_t08_mutant = """class Win32Mutant : public Win32Handle<Mutant> {
 public:
  explicit Win32Mutant(HANDLE handle) : Win32Handle(handle) {}
  ~Win32Mutant() = default;
  bool Release() override { return theseus::sync::ReleaseMutex(handle_); }
};

std::unique_ptr<Mutant> Mutant::Create(bool initial_owner) {
  auto handle = static_cast<HANDLE>(theseus::sync::CreateMutexHandle(initial_owner));
  if (handle) {
    return std::make_unique<Win32Mutant>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}"""

patch_once(threading_win_cpp, old_t08_mutant, new_t08_mutant,
           "T08 delegate mutexes to Theseus")

old_t08_timer_set_once = """    LARGE_INTEGER due_time_li;
    due_time_li.QuadPart = WClock_::to_file_time(due_time);
    auto completion_routine =
        callback_ ? reinterpret_cast<PTIMERAPCROUTINE>(CompletionRoutine) : NULL;
    return SetWaitableTimer(handle_, &due_time_li, 0, completion_routine, this, FALSE) ? true
                                                                                       : false;"""

new_t08_timer_set_once = """    const auto due_time_filetime = WClock_::to_file_time(due_time);
    const auto completion_routine =
        callback_ ? reinterpret_cast<uintptr_t>(CompletionRoutine) : uintptr_t{0};
    return theseus::sync::SetWaitableTimer(
        handle_, due_time_filetime, 0, completion_routine, this);"""

patch_once(threading_win_cpp, old_t08_timer_set_once, new_t08_timer_set_once,
           "T08 delegate one-shot timers to Theseus")

old_t08_timer_repeating = """    LARGE_INTEGER due_time_li;
    due_time_li.QuadPart = WClock_::to_file_time(due_time);
    auto completion_routine =
        callback_ ? reinterpret_cast<PTIMERAPCROUTINE>(CompletionRoutine) : NULL;
    return SetWaitableTimer(handle_, &due_time_li, int32_t(period.count()), completion_routine,
                            this, FALSE)
               ? true
               : false;"""

new_t08_timer_repeating = """    const auto due_time_filetime = WClock_::to_file_time(due_time);
    const auto completion_routine =
        callback_ ? reinterpret_cast<uintptr_t>(CompletionRoutine) : uintptr_t{0};
    return theseus::sync::SetWaitableTimer(
        handle_, due_time_filetime, int32_t(period.count()),
        completion_routine, this);"""

patch_once(threading_win_cpp, old_t08_timer_repeating, new_t08_timer_repeating,
           "T08 delegate repeating timers to Theseus")

old_t08_timer_cancel = """    return CancelWaitableTimer(handle_) ? true : false;"""
new_t08_timer_cancel = """    return theseus::sync::CancelWaitableTimer(handle_);"""

patch_once(threading_win_cpp, old_t08_timer_cancel, new_t08_timer_cancel,
           "T08 delegate timer cancellation to Theseus")

old_t08_timer_create_manual = """std::unique_ptr<Timer> Timer::CreateManualResetTimer() {
  HANDLE handle = CreateWaitableTimer(NULL, TRUE, NULL);
  if (handle) {
    return std::make_unique<Win32Timer>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}"""

new_t08_timer_create_manual = """std::unique_ptr<Timer> Timer::CreateManualResetTimer() {
  auto handle = static_cast<HANDLE>(theseus::sync::CreateWaitableTimerHandle(true));
  if (handle) {
    return std::make_unique<Win32Timer>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}"""

patch_once(threading_win_cpp, old_t08_timer_create_manual, new_t08_timer_create_manual,
           "T08 delegate manual timers to Theseus")

old_t08_timer_create_sync = """std::unique_ptr<Timer> Timer::CreateSynchronizationTimer() {
  HANDLE handle = CreateWaitableTimer(NULL, FALSE, NULL);
  if (handle) {
    return std::make_unique<Win32Timer>(handle);
  } else {
    LOG_LASTERROR();
    return nullptr;
  }
}"""

new_t08_timer_create_sync = """std::unique_ptr<Timer> Timer::CreateSynchronizationTimer() {
  auto handle = static_cast<HANDLE>(theseus::sync::CreateWaitableTimerHandle(false));
  if (handle) {
    return std::make_unique<Win32Timer>(handle);
  }
  LOG_LASTERROR();
  return nullptr;
}"""

patch_once(threading_win_cpp, old_t08_timer_create_sync, new_t08_timer_create_sync,
           "T08 delegate synchronization timers to Theseus")


# 12) Theseus T09 crash diagnostics. Install the process-wide crash handler
# before the native launcher and before ReXGlue initializes any subsystems.
windowed_main = Path("tools/rexglue/src/ui/windowed_app_main_sdl.cpp")

old_t09_crash_include = """#include <rex/platform.h>
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)
#include "asura_launcher_win.h"
#endif
#include <rex/ui/windowed_app.h>"""

new_t09_crash_include = """#include <rex/platform.h>
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)
#include "asura_launcher_win.h"
#include "platform/theseus_crash.h"
#endif
#include <rex/ui/windowed_app.h>"""

patch_once(windowed_main, old_t09_crash_include, new_t09_crash_include,
           "T09 early crash diagnostics include")

old_t09_console_entry = """int main(int argc, char* argv[]) {
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)"""

new_t09_console_entry = """int main(int argc, char* argv[]) {
#if REX_PLATFORM_WIN32 && defined(ASURA_NATIVE_LAUNCHER)
  theseus::crash::Install();
  theseus::crash::Breadcrumb("entry: console main");
"""

patch_once(windowed_main, old_t09_console_entry, new_t09_console_entry,
           "T09 install diagnostics before console launcher")

old_t09_windows_entry = """int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
  (void)hinstance;"""

new_t09_windows_entry = """int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
#if defined(ASURA_NATIVE_LAUNCHER)
  theseus::crash::Install();
  theseus::crash::Breadcrumb("entry: wWinMain");
#endif
  (void)hinstance;"""

patch_once(windowed_main, old_t09_windows_entry, new_t09_windows_entry,
           "T09 install diagnostics before Windows launcher")


# 13) Theseus T09.3 native input serialization fix.
# T09.2b ASan repeatedly caught DeviceInfo/std::string destruction while
# multiple guest XThreads could concurrently enter XInput polling paths.
# Serialize the public input surface so RefreshDevices cannot mutate or clear
# DeviceInfo containers concurrently. This is the only runtime behavior change
# carried forward from the T09.2 diagnostic branch.
input_system_h = Path("tools/rexglue/include/rex/input/input_system.h")

old_t093_mutex_include = """#include <memory>
#include <vector>"""

new_t093_mutex_include = """#include <memory>
#include <mutex>
#include <vector>"""

patch_once(input_system_h, old_t093_mutex_include, new_t093_mutex_include,
           "T09.3 InputSystem mutex include")

old_t093_mutex_member = """  rex::ui::Window* window_ = nullptr;

  std::vector<std::unique_ptr<InputDriver>> drivers_;"""

new_t093_mutex_member = """  rex::ui::Window* window_ = nullptr;

  // Guest XThreads may poll XInput concurrently. RefreshDevices mutates
  // vectors containing std::string-bearing DeviceInfo objects, so all public
  // stateful input operations are serialized through this host mutex.
  std::mutex state_mutex_;

  std::vector<std::unique_ptr<InputDriver>> drivers_;"""

patch_once(input_system_h, old_t093_mutex_member, new_t093_mutex_member,
           "T09.3 InputSystem state mutex")

input_system_cpp = Path("tools/rexglue/src/input/input_system.cpp")

_t093_methods = [
    (
        """void InputSystem::Shutdown() {
  // device_owners_ holds raw driver pointers.""",
        """void InputSystem::Shutdown() {
  std::scoped_lock lock(state_mutex_);
  // device_owners_ holds raw driver pointers.""",
        "T09.3 serialize input shutdown",
    ),
    (
        """void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));""",
        """void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  std::scoped_lock lock(state_mutex_);
  drivers_.push_back(std::move(driver));""",
        "T09.3 serialize driver add",
    ),
    (
        """void InputSystem::SetActiveCallback(std::function<bool()> callback) {
  for (auto& driver : drivers_) {""",
        """void InputSystem::SetActiveCallback(std::function<bool()> callback) {
  std::scoped_lock lock(state_mutex_);
  for (auto& driver : drivers_) {""",
        "T09.3 serialize active callback",
    ),
    (
        """void InputSystem::SetDeviceAssignment(std::unique_ptr<DeviceAssignment> assignment) {
  assignment_ = std::move(assignment);""",
        """void InputSystem::SetDeviceAssignment(std::unique_ptr<DeviceAssignment> assignment) {
  std::scoped_lock lock(state_mutex_);
  assignment_ = std::move(assignment);""",
        "T09.3 serialize device assignment",
    ),
    (
        """X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");""",
        """X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  std::scoped_lock lock(state_mutex_);
  SCOPE_profile_cpu_f("hid");""",
        "T09.3 serialize GetCapabilities",
    ),
    (
        """X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");""",
        """X_RESULT InputSystem::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  std::scoped_lock lock(state_mutex_);
  SCOPE_profile_cpu_f("hid");""",
        "T09.3 serialize GetState",
    ),
    (
        """X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");""",
        """X_RESULT InputSystem::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  std::scoped_lock lock(state_mutex_);
  SCOPE_profile_cpu_f("hid");""",
        "T09.3 serialize SetState",
    ),
    (
        """X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");""",
        """X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  std::scoped_lock lock(state_mutex_);
  SCOPE_profile_cpu_f("hid");""",
        "T09.3 serialize GetKeystroke",
    ),
]

# AttachWindow has a slightly different upstream spelling, so keep it separate.
text = input_system_cpp.read_text(encoding="utf-8")
attach_begin = text.find("void InputSystem::AttachWindow")
attach_end = text.find("void InputSystem::SetActiveCallback")
if "std::scoped_lock lock(state_mutex_);" not in text[attach_begin:attach_end]:
    old_t093_attach = """void InputSystem::AttachWindow(rex::ui::Window* window) {
  window_ = window;"""
    new_t093_attach = """void InputSystem::AttachWindow(rex::ui::Window* window) {
  std::scoped_lock lock(state_mutex_);
  window_ = window;"""
    patch_once(input_system_cpp, old_t093_attach, new_t093_attach,
               "T09.3 serialize window attach")
else:
    print("T09.3 serialize window attach: already applied")

for old, new, label in _t093_methods:
    patch_once(input_system_cpp, old, new, label)
