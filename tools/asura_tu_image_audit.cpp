#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/types.h>

namespace fs = std::filesystem;

namespace {

struct TempTree {
  fs::path path;
  ~TempTree() {
    if (!path.empty()) {
      std::error_code ec;
      fs::remove_all(path, ec);
    }
  }
};

std::string Hex32(uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::uppercase << std::hex << std::setw(8)
      << std::setfill('0') << value;
  return out.str();
}

std::string JsonEscape(std::string_view text) {
  std::ostringstream out;
  for (char ch : text) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<unsigned>(static_cast<unsigned char>(ch));
        } else {
          out << ch;
        }
        break;
    }
  }
  return out.str();
}

void Usage() {
  std::cerr
      << "Asura TU01 Offline Image Audit\n\n"
      << "Usage:\n"
      << "  asura_tu_image_audit --base <default.xex> --xexp <default.xexp> "
         "--out <tu01_image.bin> [--metadata <tu01_image.json>]\n\n"
      << "The preserved inputs are opened read-only, copied into a temporary "
         "workspace, and never modified.\n";
}

bool CopyPreserved(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  if (!fs::is_regular_file(from, ec)) {
    std::cerr << "Input does not exist or is not a regular file: " << from << "\n";
    return false;
  }
  fs::copy_file(from, to, fs::copy_options::none, ec);
  if (ec) {
    std::cerr << "Failed to stage " << from << ": " << ec.message() << "\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  fs::path base;
  fs::path xexp;
  fs::path output;
  fs::path metadata;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    auto take_path = [&](fs::path& target) -> bool {
      if (i + 1 >= argc) {
        std::cerr << "Missing value after " << arg << "\n";
        return false;
      }
      target = fs::u8path(argv[++i]);
      return true;
    };

    if (arg == "--base") {
      if (!take_path(base)) return 2;
    } else if (arg == "--xexp") {
      if (!take_path(xexp)) return 2;
    } else if (arg == "--out") {
      if (!take_path(output)) return 2;
    } else if (arg == "--metadata") {
      if (!take_path(metadata)) return 2;
    } else if (arg == "--help" || arg == "-h") {
      Usage();
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      Usage();
      return 2;
    }
  }

  if (base.empty() || xexp.empty() || output.empty()) {
    Usage();
    return 2;
  }
  if (metadata.empty()) {
    metadata = output;
    metadata += ".json";
  }

  std::error_code ec;
  const auto base_abs = fs::absolute(base, ec);
  if (ec) {
    std::cerr << "Could not resolve base XEX path: " << ec.message() << "\n";
    return 3;
  }
  ec.clear();
  const auto xexp_abs = fs::absolute(xexp, ec);
  if (ec) {
    std::cerr << "Could not resolve XEXP path: " << ec.message() << "\n";
    return 3;
  }

  auto temp_root = fs::temp_directory_path(ec);
  if (ec) {
    std::cerr << "Could not resolve temporary directory: " << ec.message() << "\n";
    return 3;
  }

  // A per-process staging tree means the preserved source files never need to
  // be renamed, patched, moved, or opened writable.
  TempTree staging{temp_root / ("asura_tu01_audit_" + std::to_string(
#if defined(_WIN32)
      static_cast<unsigned long>(GetCurrentProcessId())
#else
      static_cast<unsigned long>(::getpid())
#endif
      ))};

  fs::remove_all(staging.path, ec);
  ec.clear();
  const auto game_dir = staging.path / "game";
  fs::create_directories(game_dir, ec);
  if (ec) {
    std::cerr << "Could not create temporary staging directory: " << ec.message() << "\n";
    return 3;
  }

  if (!CopyPreserved(base_abs, game_dir / "default.xex") ||
      !CopyPreserved(xexp_abs, game_dir / "default.xexp")) {
    return 4;
  }

  rex::Runtime runtime(game_dir, staging.path / "user");
  rex::RuntimeConfig config{};
  config.kernel_init = rex::kernel::InitializeKernel;
  config.tool_mode = true;

  const X_STATUS setup = runtime.Setup(std::move(config));
  if (XFAILED(setup)) {
    std::cerr << "ReXGlue tool-mode runtime setup failed: " << Hex32(setup) << "\n";
    return 5;
  }

  const X_STATUS load = runtime.LoadXexImage("game:\\default.xex");
  if (XFAILED(load)) {
    std::cerr << "Base XEX + XEXP load/patch failed: " << Hex32(load) << "\n";
    return 6;
  }

  auto executable = runtime.kernel_state()->GetExecutableModule();
  auto* xex_module = executable ? executable->xex_module() : nullptr;
  if (!xex_module) {
    std::cerr << "Patched executable module is unavailable.\n";
    return 7;
  }

  const uint32_t image_base = xex_module->base_address();
  const uint32_t image_size = xex_module->image_size();
  const uint32_t entry_point = xex_module->entry_point();
  auto* image = runtime.memory()->TranslateVirtual<uint8_t*>(image_base);
  if (!image || image_size == 0) {
    std::cerr << "Patched guest image is empty or unmapped.\n";
    return 8;
  }

  // Asura TU01 invariant already established by the preservation project.
  constexpr uint32_t kExpectedTu01EntryPoint = 0x82B75160;
  if (entry_point != kExpectedTu01EntryPoint) {
    std::cerr << "TU01 entrypoint mismatch. Loaded " << Hex32(entry_point)
              << ", expected " << Hex32(kExpectedTu01EntryPoint) << "\n";
    return 9;
  }

  if (!output.parent_path().empty()) {
    fs::create_directories(output.parent_path(), ec);
    if (ec) {
      std::cerr << "Could not create output directory: " << ec.message() << "\n";
      return 10;
    }
  }

  {
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::cerr << "Could not create output image: " << output << "\n";
      return 10;
    }
    out.write(reinterpret_cast<const char*>(image),
              static_cast<std::streamsize>(image_size));
    if (!out.good()) {
      std::cerr << "Failed while writing output image.\n";
      return 10;
    }
  }

  if (!metadata.parent_path().empty()) {
    fs::create_directories(metadata.parent_path(), ec);
    if (ec) {
      std::cerr << "Could not create metadata directory: " << ec.message() << "\n";
      return 11;
    }
  }

  std::ofstream meta(metadata, std::ios::trunc);
  if (!meta) {
    std::cerr << "Could not create metadata file: " << metadata << "\n";
    return 11;
  }

  meta << "{\n";
  meta << "  \"schema\": 1,\n";
  meta << "  \"kind\": \"asura-tu01-patched-guest-image\",\n";
  meta << "  \"derived_only\": true,\n";
  meta << "  \"redistribute\": false,\n";
  meta << "  \"image_base\": \"" << Hex32(image_base) << "\",\n";
  meta << "  \"image_size\": " << image_size << ",\n";
  meta << "  \"entry_point\": \"" << Hex32(entry_point) << "\",\n";

  if (const auto* exec = xex_module->opt_execution_info()) {
    const auto version = exec->version();
    meta << "  \"title_id\": \"" << Hex32(exec->title_id) << "\",\n";
    meta << "  \"media_id\": \"" << Hex32(exec->media_id) << "\",\n";
    meta << "  \"version\": \"" << static_cast<unsigned>(version.major) << "."
         << static_cast<unsigned>(version.minor) << "."
         << static_cast<unsigned>(version.build) << "."
         << static_cast<unsigned>(version.qfe) << "\",\n";
  }

  meta << "  \"sections\": [\n";
  const auto sections = xex_module->binary_sections();
  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    meta << "    {"
         << "\"name\": \"" << JsonEscape(section.name) << "\", "
         << "\"address\": \"" << Hex32(section.virtual_address) << "\", "
         << "\"size\": " << section.virtual_size << ", "
         << "\"executable\": " << (section.executable ? "true" : "false") << ", "
         << "\"writable\": " << (section.writable ? "true" : "false")
         << "}";
    if (i + 1 != sections.size()) meta << ",";
    meta << "\n";
  }
  meta << "  ]\n";
  meta << "}\n";

  if (!meta.good()) {
    std::cerr << "Failed while writing metadata.\n";
    return 11;
  }

  std::cout << "TU01 patched guest image exported successfully.\n"
            << "  image: " << output << "\n"
            << "  metadata: " << metadata << "\n"
            << "  base: " << Hex32(image_base) << "\n"
            << "  size: " << image_size << "\n"
            << "  entrypoint: " << Hex32(entry_point) << "\n";
  return 0;
}
