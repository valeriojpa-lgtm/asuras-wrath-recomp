#include "platform/theseus_heap_probe.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace theseus::heap_probe {
namespace {

#if defined(_WIN32)
std::filesystem::path ExecutableFolder() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) {
    return std::filesystem::current_path();
  }
  buffer.resize(length);
  return std::filesystem::path(buffer).parent_path();
}
#endif

}  // namespace

void ConfigureChildEnvironment() noexcept {
#if defined(_WIN32) && defined(ASURA_HEAP_PROBE)
  const auto root = ExecutableFolder();
  std::error_code ec;
  std::filesystem::create_directories(
      root / "UserData" / "Logs" / "Crashes", ec);

  // Keep the path relative to the executable working directory so ASAN_OPTIONS
  // doesn't need to embed a drive-letter colon inside its colon-delimited
  // option string.
  SetEnvironmentVariableA(
      "ASAN_OPTIONS",
      "abort_on_error=1:"
      "halt_on_error=1:"
      "detect_leaks=0:"
      "symbolize=1:"
      "fast_unwind_on_malloc=0:"
      "log_exe_name=1:"
      "log_path=UserData/Logs/Crashes/AsanCrash");

  const auto symbolizer = root / "llvm-symbolizer.exe";
  if (std::filesystem::exists(symbolizer, ec)) {
    SetEnvironmentVariableW(
        L"ASAN_SYMBOLIZER_PATH", symbolizer.c_str());
    SetEnvironmentVariableW(
        L"LLVM_SYMBOLIZER_PATH", symbolizer.c_str());
  }
#endif
}

[[noreturn]] void TriggerAsanSelfTest() {
#if defined(ASURA_HEAP_PROBE)
  auto* allocation = new unsigned char[8];
  volatile unsigned char* probe = allocation;

  // Intentionally out of bounds. The CI workflow requires AddressSanitizer to
  // catch this write and emit "heap-buffer-overflow" before the delete below.
  probe[16] = 0xA5;

  delete[] allocation;
#endif
  std::abort();
}

}  // namespace theseus::heap_probe
