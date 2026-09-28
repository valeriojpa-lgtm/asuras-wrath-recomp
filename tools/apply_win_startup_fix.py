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


# 7) TU01 runtime patch: ReXGlue normally looks only for a sibling
# "default.xexp" next to default.xex. Asura's portable preservation layout keeps
# the preserved retail XEX untouched and stores the verified derived XEXP under
# Data/Update/default.xexp. Fall back to update: when no sibling patch exists.
user_module = Path("tools/rexglue/src/system/user_module.cpp")

old_patch_lookup = """  // Search for sibling XEX patch file
  auto patch_entry = kernel_state_->file_system()->ResolvePath(path_ + "p");
  if (patch_entry) {
    auto patch_path = patch_entry->absolute_path();

    REXSYS_DEBUG("Loading XEX patch from {}", patch_path);"""

new_patch_lookup = """  // Search for an XEX patch only while loading a normal module.
  // An XEXP is itself a patch module. If a patch module falls back to
  // update:\\default.xexp it resolves itself and recursively reloads forever.
  rex::filesystem::Entry* patch_entry = nullptr;
  if (!xex_module()->is_patch()) {
    patch_entry = kernel_state_->file_system()->ResolvePath(path_ + "p");
    if (!patch_entry) {
      patch_entry = kernel_state_->file_system()->ResolvePath("update:\\\\default.xexp");
    }
  }
  if (patch_entry) {
    auto patch_path = patch_entry->absolute_path();

    REXSYS_INFO("Loading XEX patch from {}", patch_path);"""

patch_once(user_module, old_patch_lookup, new_patch_lookup,
           "TU01 update-device XEXP fallback")


# 8) TU01 diagnostics: if codegen misses an indirect-call entrypoint, dump the
# real PPC words from the already-patched guest image before trapping. This
# avoids guessing aliases and lets us reconstruct the exact omitted thunk.
dispatcher = Path("tools/rexglue/src/system/function_dispatcher.cpp")

old_invalid_trap = """static void InvalidFunctionTrap(PPCContext& ctx, uint8_t* /*base*/) {
  REX_FATAL("Call to invalid or unregistered function at guest address 0x{:08X}",
            ctx.last_indirect_target);
}"""

new_invalid_trap = """static void InvalidFunctionTrap(PPCContext& ctx, uint8_t* base) {
  const uint32_t address = ctx.last_indirect_target;
  uint32_t words[8] = {};
  for (size_t i = 0; i < 8; ++i) {
    words[i] = memory::load_and_swap<uint32_t>(base + address + uint32_t(i * 4));
  }

  REXLOG_ERROR(
      "Unregistered PPC target {:08X} context: "
      "LR={:08X} CTR={:08X} SP(r1)={:08X} TOC(r2)={:08X} "
      "r3={:08X} r4={:08X} r5={:08X} r6={:08X}",
      address, static_cast<uint32_t>(ctx.lr), ctx.ctr.u32,
      ctx.r1.u32, ctx.r2.u32, ctx.r3.u32, ctx.r4.u32,
      ctx.r5.u32, ctx.r6.u32);

  REXLOG_ERROR(
      "Unregistered PPC target {:08X} words: "
      "{:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}",
      address, words[0], words[1], words[2], words[3],
      words[4], words[5], words[6], words[7]);

  REX_FATAL("Call to invalid or unregistered function at guest address 0x{:08X}",
            address);
}"""

patch_once(dispatcher, old_invalid_trap, new_invalid_trap,
           "TU01 missing-entrypoint PPC dump")


# 9) Conservative runtime recovery for codegen-missed PPC branch veneers.
# Only a pure unconditional branch with LK=0 is auto-resolved. This preserves
# guest semantics while eliminating a common class of indirect-call misses.
old_resolve_indirect = """PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  FunctionDispatcher* dispatcher = GetBoundFunctionDispatcher();
  if (!dispatcher) {
    return &InvalidFunctionTrap;
  }

  if (PPCFunc* func = dispatcher->GetFunction(guest_address)) {
    return func;
  }

  return &InvalidFunctionTrap;
}"""

new_resolve_indirect = """static PPCFunc* TryResolveBranchVeneer(FunctionDispatcher* dispatcher,
                                            uint32_t guest_address) {
  Runtime* runtime = Runtime::instance();
  if (!runtime || !runtime->memory()) {
    return nullptr;
  }

  uint32_t current = guest_address;
  for (uint32_t depth = 0; depth < 8; ++depth) {
    // Never inspect arbitrary guest memory. A veneer candidate must belong to
    // one of the registered module code/thunk ranges before we read its PPC.
    if (dispatcher->FindCallerModuleBase(current) == 0) {
      REXLOG_DEBUG("PPC veneer candidate {:08X} is outside registered module ranges",
                   current);
      break;
    }

    const uint8_t* p = runtime->memory()->TranslateVirtual(current);
    const uint32_t insn = memory::load_and_swap<uint32_t>(p);

    // PowerPC opcode 18 = b / ba / bl / bla. Auto-resolution is only safe
    // for LK=0 because a link-setting branch has observable LR semantics.
    if ((insn >> 26) != 18 || (insn & 1u) != 0) {
      break;
    }

    uint32_t disp_bits = insn & 0x03FFFFFCu;
    if (disp_bits & 0x02000000u) {
      disp_bits |= 0xFC000000u;
    }
    const int32_t displacement = static_cast<int32_t>(disp_bits);
    const bool absolute = (insn & 2u) != 0;
    const uint32_t target =
        absolute ? static_cast<uint32_t>(displacement)
                 : current + static_cast<uint32_t>(displacement);

    if (target == current) {
      break;
    }

    if (PPCFunc* func = dispatcher->GetFunction(target)) {
      // Cache the original veneer address so future calls are O(1).
      if (dispatcher->SetFunction(guest_address, func)) {
        REXLOG_INFO("Auto-resolved PPC branch veneer {:08X} -> {:08X}",
                    guest_address, target);
      }
      return func;
    }

    current = target;
  }

  return nullptr;
}

PPCFunc* ResolveIndirectFunction(uint32_t guest_address) {
  FunctionDispatcher* dispatcher = GetBoundFunctionDispatcher();
  if (!dispatcher) {
    return &InvalidFunctionTrap;
  }

  if (PPCFunc* func = dispatcher->GetFunction(guest_address)) {
    return func;
  }

  if (PPCFunc* func = TryResolveBranchVeneer(dispatcher, guest_address)) {
    return func;
  }

  return &InvalidFunctionTrap;
}"""

patch_once(dispatcher, old_resolve_indirect, new_resolve_indirect,
           "TU01 conservative PPC branch-veneer resolver")


# 10) Conservative codegen discovery for literal PPC indirect-call targets.
# The legacy ReXGlue functionPointerScan accepts any lis/addi|ori pair that
# merely forms a code address. We keep that broad scanner disabled. This patch
# auto-registers only a proven constant -> CTR -> link-setting indirect call.
phase_discover = Path("tools/rexglue/src/codegen/phase_discover.cpp")

old_discover = "namespace phases {\n\nVoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {\n  (void)reporter;\n  discoverAllFunctions(ctx);\n  return Ok();\n}"
new_discover = "namespace {\n\nsize_t conservativeIndirectCallScan(CodegenContext& ctx) {\n  if (!ctx.hasDecoded()) {\n    return 0;\n  }\n\n  auto& graph = ctx.graph;\n  auto& binary = ctx.binary();\n  auto& decoded = ctx.decoded();\n\n  size_t proven = 0;\n  size_t highConfidence = 0;\n  size_t alreadyKnown = 0;\n  size_t rejected = 0;\n\n  for (const auto& region : decoded.codeRegions()) {\n    if (region.end < region.start + 16) {\n      continue;\n    }\n\n    for (uint32_t addr = region.start; addr + 12 < region.end; addr += 4) {\n      const auto* lis = decoded.get(addr);\n      const auto* low = decoded.get(addr + 4);\n      const auto* mtctr = decoded.get(addr + 8);\n      const auto* branch = decoded.get(addr + 12);\n      if (!lis || !low || !mtctr || !branch || !isLis(*lis)) {\n        continue;\n      }\n\n      const uint32_t reg = lis->D.RT;\n      uint32_t target = 0;\n      bool materialized = false;\n\n      if (low->opcode == Opcode::addi &&\n          low->D.RA == reg && low->D.RT == reg) {\n        const uint32_t hi =\n            static_cast<uint32_t>(static_cast<int16_t>(lis->D.d)) << 16;\n        const int16_t lo = static_cast<int16_t>(low->D.d);\n        target = hi + static_cast<int32_t>(lo);\n        materialized = true;\n      } else if (low->opcode == Opcode::ori &&\n                 low->D.RT == reg && low->D.RA == reg) {\n        const uint32_t hi =\n            static_cast<uint32_t>(static_cast<int16_t>(lis->D.d)) << 16;\n        target = hi | static_cast<uint16_t>(low->D.d);\n        materialized = true;\n      }\n\n      if (!materialized ||\n          mtctr->opcode != Opcode::mtctr ||\n          mtctr->XFX.RS() != reg) {\n        continue;\n      }\n\n      const bool isCall = branch->opcode == Opcode::bcctrl;\n      const bool isCtrJump = branch->opcode == Opcode::bcctr;\n      if (!isCall && !isCtrJump) {\n        continue;\n      }\n\n      // A byte pattern in an executable region is not sufficient proof.\n      // Require the complete materialize->CTR->branch sequence to belong to\n      // the same function already discovered by the graph.\n      const auto* owner = graph.getFunctionContaining(addr);\n      if (!owner ||\n          graph.getFunctionContaining(addr + 4) != owner ||\n          graph.getFunctionContaining(addr + 8) != owner ||\n          graph.getFunctionContaining(addr + 12) != owner) {\n        ++rejected;\n        REXCODEGEN_TRACE(\n            \"conservativeIndirectCallScan: REJECT cross-function pattern at 0x{:08X}\",\n            addr);\n        continue;\n      }\n\n      const auto* targetInsn = decoded.get(target);\n      if ((target & 3u) != 0 || !decoded.isInCodeRegion(target) ||\n          binary.isInImportExportRange(target) || !targetInsn ||\n          isInvalid(*targetInsn)) {\n        ++rejected;\n        REXCODEGEN_TRACE(\n            \"conservativeIndirectCallScan: REJECT 0x{:08X} from 0x{:08X}\",\n            target, addr);\n        continue;\n      }\n\n      if (graph.getFunction(target) || graph.isImport(target)) {\n        ++alreadyKnown;\n        continue;\n      }\n\n      if (!isCall) {\n        ++highConfidence;\n        REXCODEGEN_DEBUG(\n            \"conservativeIndirectCallScan: HIGH_CONFIDENCE 0x{:08X} \"\n            \"via lis/materialize/mtctr/bcctr at 0x{:08X}; not auto-adding\",\n            target, addr);\n        continue;\n      }\n\n      graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);\n      ++proven;\n      REXCODEGEN_DEBUG(\n          \"conservativeIndirectCallScan: PROVEN 0x{:08X} \"\n          \"via lis/materialize/mtctr/bcctrl at 0x{:08X}\",\n          target, addr);\n    }\n  }\n\n  REXCODEGEN_TRACE(\n      \"conservativeIndirectCallScan: {} PROVEN added, {} HIGH_CONFIDENCE \"\n      \"reported, {} already known, {} rejected\",\n      proven, highConfidence, alreadyKnown, rejected);\n  return proven;\n}\n\n}  // anonymous namespace\n\nnamespace phases {\n\nVoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {\n  (void)reporter;\n  discoverAllFunctions(ctx);\n\n  if (conservativeIndirectCallScan(ctx) > 0) {\n    size_t iteration = 0;\n    constexpr size_t kMaxIndirectIterations = 8;\n    while (iteration++ < kMaxIndirectIterations) {\n      auto knownFunctions = buildKnownFunctions(ctx.graph);\n      if (discoverPendingFunctions(ctx, knownFunctions) == 0) {\n        break;\n      }\n    }\n  }\n\n  return Ok();\n}"

patch_once(phase_discover, old_discover, new_discover,
           "conservative proven indirect-call discovery")


# 11) Conservative pointer-table discovery without MSVC RTTI.
# ReXGlue's VTableScanner requires a Complete Object Locator and .?AV/.?AU
# type descriptors. Xenon/UE3 pointer tables can legitimately omit that RTTI.
# This secondary pass does NOT register arbitrary code-looking dwords:
#   - table run must contain >=3 consecutive aligned executable pointers;
#   - at least 2 sibling slots must already be known functions/imports;
#   - candidate must lie in an uncovered gap (not inside an existing function);
#   - candidate bytes must prove a tiny thunk:
#       b known_target
#       or addi r3,r3,imm ; b known_target
# Everything else is diagnostic-only.
pointer_include_anchor = """#include <unordered_set>"""
pointer_include_replacement = """#include <unordered_set>
#include <vector>"""
patch_once(phase_discover, pointer_include_anchor, pointer_include_replacement,
           "pointer-table scanner vector include")

pointer_helper_anchor = """}  // anonymous namespace

namespace phases {

VoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {"""

pointer_helper_replacement = r"""bool IsKnownCallable(FunctionGraph& graph, uint32_t address) {
  return graph.getFunction(address) != nullptr || graph.isImport(address);
}

bool IsStrongFunctionBoundary(CodegenContext& ctx, uint32_t address) {
  if (address < 4 || !ctx.hasDecoded()) {
    return false;
  }

  const auto* previous = ctx.decoded().get(address - 4);
  if (!previous) {
    return false;
  }

  if (previous->raw == 0) {
    return true;
  }

  if (previous->is_return() ||
      previous->opcode == Opcode::b ||
      previous->opcode == Opcode::ba) {
    return true;
  }

  if (previous->opcode == Opcode::bcctr &&
      previous->raw == 0x4E800420u) {
    return true;
  }

  return false;
}

bool IsProvenTinyThunk(CodegenContext& ctx, uint32_t address,
                       uint32_t* outTailTarget) {
  auto& graph = ctx.graph;
  auto& decoded = ctx.decoded();

  const auto* first = decoded.get(address);
  if (!first || isInvalid(*first)) {
    return false;
  }

  // Pure non-linking veneer: b known_target.
  if (first->opcode == Opcode::b && first->branch_target &&
      IsKnownCallable(graph, *first->branch_target)) {
    if (outTailTarget) {
      *outTailTarget = *first->branch_target;
    }
    return true;
  }

  // C++ this-adjustor thunk seen throughout Asura:
  //   addi r3,r3,imm
  //   b    known_target
  if (first->opcode != Opcode::addi ||
      first->D.RT != 3 || first->D.RA != 3) {
    return false;
  }

  const auto* second = decoded.get(address + 4);
  if (!second || second->opcode != Opcode::b ||
      !second->branch_target ||
      !IsKnownCallable(graph, *second->branch_target)) {
    return false;
  }

  if (outTailTarget) {
    *outTailTarget = *second->branch_target;
  }
  return true;
}

size_t conservativePointerTableScan(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    return 0;
  }

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();

  size_t proven = 0;
  size_t highConfidence = 0;
  size_t tables = 0;
  size_t rejected = 0;

  auto plausiblePointer = [&](uint32_t value) {
    if ((value & 3u) != 0 ||
        binary.isInImportExportRange(value) ||
        !decoded.isInCodeRegion(value)) {
      return false;
    }
    const auto* insn = decoded.get(value);
    return insn && !isInvalid(*insn);
  };

  for (const auto& section : binary.sections()) {
    if (section.executable || !section.data || section.size < 12) {
      continue;
    }

    struct Slot {
      uint32_t slotAddress;
      uint32_t target;
    };
    std::vector<Slot> run;

    auto flushRun = [&]() {
      if (run.size() < 3) {
        run.clear();
        return;
      }

      size_t knownSiblings = 0;
      for (const auto& slot : run) {
        if (IsKnownCallable(graph, slot.target)) {
          ++knownSiblings;
        }
      }

      // Two independent known entries are the minimum evidence that this is a
      // callable pointer table rather than arbitrary binary data.
      if (knownSiblings < 2) {
        run.clear();
        return;
      }

      ++tables;
      for (const auto& slot : run) {
        const uint32_t target = slot.target;
        if (IsKnownCallable(graph, target)) {
          continue;
        }

        // A pointer into the middle of a known function may be a secondary
        // entrypoint, but adding overlapping functions automatically is not
        // conservative enough. Report it only.
        if (graph.getFunctionContaining(target)) {
          ++highConfidence;
          REXCODEGEN_DEBUG(
              "conservativePointerTableScan: HIGH_CONFIDENCE overlapping "
              "target 0x{:08X} from table slot 0x{:08X}; not auto-adding",
              target, slot.slotAddress);
          continue;
        }

        uint32_t tailTarget = 0;
        if (IsProvenTinyThunk(ctx, target, &tailTarget)) {
          graph.addFunction(target, 4, FunctionAuthority::VTABLE, true);
          ++proven;
          REXCODEGEN_DEBUG(
              "conservativePointerTableScan: PROVEN tiny thunk 0x{:08X} -> "
              "0x{:08X} from table slot 0x{:08X} ({} slots, {} known siblings)",
              target, tailTarget, slot.slotAddress, run.size(), knownSiblings);
          continue;
        }

        const bool strongTable =
            run.size() >= 8 &&
            knownSiblings >= 4 &&
            knownSiblings * 2 >= run.size();

        if (strongTable && IsStrongFunctionBoundary(ctx, target)) {
          graph.addFunction(target, 4, FunctionAuthority::VTABLE, true);
          ++proven;
          REXCODEGEN_DEBUG(
              "conservativePointerTableScan: PROVEN RTTI-less method "
              "0x{:08X} from table slot 0x{:08X} "
              "({} slots, {} known siblings, strong entry boundary)",
              target, slot.slotAddress, run.size(), knownSiblings);
          continue;
        }

        ++highConfidence;
        REXCODEGEN_DEBUG(
            "conservativePointerTableScan: HIGH_CONFIDENCE unknown code "
            "target 0x{:08X} from table slot 0x{:08X}; insufficient "
            "table/boundary proof, not auto-adding",
            target, slot.slotAddress);
      }

      run.clear();
    };

    const uint32_t sectionEnd = section.baseAddress + section.size;
    for (uint32_t slotAddress = section.baseAddress;
         slotAddress + 4 <= sectionEnd; slotAddress += 4) {
      const uint8_t* host = section.translate(slotAddress);
      if (!host) {
        flushRun();
        continue;
      }
      const uint32_t value = load_and_swap<uint32_t>(host);
      if (!plausiblePointer(value)) {
        flushRun();
        continue;
      }
      run.push_back({slotAddress, value});
    }
    flushRun();
  }

  REXCODEGEN_TRACE(
      "conservativePointerTableScan: {} pointer tables, {} PROVEN added, "
      "{} HIGH_CONFIDENCE reported, {} rejected",
      tables, proven, highConfidence, rejected);
  return proven;
}

}  // anonymous namespace

namespace phases {

VoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {"""

patch_once(phase_discover, pointer_helper_anchor, pointer_helper_replacement,
           "conservative non-RTTI pointer-table discovery")

pointer_call_anchor = """  if (conservativeIndirectCallScan(ctx) > 0) {
    size_t iteration = 0;
    constexpr size_t kMaxIndirectIterations = 8;
    while (iteration++ < kMaxIndirectIterations) {
      auto knownFunctions = buildKnownFunctions(ctx.graph);
      if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
        break;
      }
    }
  }

  return Ok();"""

pointer_call_replacement = """  size_t newIndirectTargets = conservativeIndirectCallScan(ctx);
  size_t newPointerTargets = conservativePointerTableScan(ctx);

  if (newIndirectTargets + newPointerTargets > 0) {
    size_t iteration = 0;
    constexpr size_t kMaxIndirectIterations = 8;
    while (iteration++ < kMaxIndirectIterations) {
      auto knownFunctions = buildKnownFunctions(ctx.graph);
      if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
        break;
      }

      // Newly discovered functions can make another pointer-table candidate
      // provable. Re-scan to a small fixed point without widening the rules.
      newPointerTargets = conservativePointerTableScan(ctx);
      if (newPointerTargets == 0 && ctx.graph.getPendingFunctions().empty()) {
        break;
      }
    }
  }

  return Ok();"""

patch_once(phase_discover, pointer_call_anchor, pointer_call_replacement,
           "run conservative pointer-table discovery")


# 12) Shutdown diagnostics only: preserve ReXGlue's safe hard-exit policy, but
# make cooperative-shutdown stragglers visible in the final log so a future
# clean-exit change can be based on evidence rather than guesswork.
kernel_state_cpp = Path("tools/rexglue/src/system/kernel_state.cpp")

shutdown_anchor = """  WaitForThreadsToExit(target_threads, kCooperativeExitTimeoutMs);

  // Stragglers are deliberately left running, never force-killed: TerminateThread
  // orphans whatever host lock the thread holds (CRT heap, mutexes) and deadlocks
  // teardown. Window close hard-exits and lets the OS reap them."""

shutdown_replacement = """  WaitForThreadsToExit(target_threads, kCooperativeExitTimeoutMs);

  size_t shutdown_stragglers = 0;
  for (const auto& thread : target_threads) {
    if (!thread->is_running()) {
      continue;
    }
    ++shutdown_stragglers;
    REXSYS_WARN(
        "TerminateTitle: guest thread {} ('{}') did not exit cooperatively "
        "within {} ms",
        thread->thread_id(), thread->name(), kCooperativeExitTimeoutMs);
  }
  if (shutdown_stragglers == 0) {
    REXSYS_INFO("TerminateTitle: all guest threads exited cooperatively");
  } else {
    REXSYS_WARN("TerminateTitle: {} guest thread(s) remain; retaining safe hard-exit policy",
                shutdown_stragglers);
  }

  // Stragglers are deliberately left running, never force-killed: TerminateThread
  // orphans whatever host lock the thread holds (CRT heap, mutexes) and deadlocks
  // teardown. Window close hard-exits and lets the OS reap them."""

patch_once(kernel_state_cpp, shutdown_anchor, shutdown_replacement,
           "cooperative shutdown straggler diagnostics")
