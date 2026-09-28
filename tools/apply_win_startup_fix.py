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
