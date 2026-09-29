#include "platform/theseus_stability_guard.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace theseus::stability {
namespace {

#if defined(_WIN32)

constexpr DWORD kTheseusHardExitSelfTest = 0xE0425498u;

std::filesystem::path ExecutablePath() {
  std::wstring buffer(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) {
    return {};
  }
  buffer.resize(length);
  return std::filesystem::path(buffer);
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty()) {
    return {};
  }
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
      nullptr, 0);
  if (length <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
      result.data(), length);
  return result;
}

// Windows CreateProcess command-line quoting compatible with CommandLineToArgvW.
std::wstring QuoteArgument(std::wstring_view argument) {
  if (argument.empty()) {
    return L"\"\"";
  }

  const bool needs_quotes =
      argument.find_first_of(L" \t\n\v\"") != std::wstring_view::npos;
  if (!needs_quotes) {
    return std::wstring(argument);
  }

  std::wstring result;
  result.push_back(L'"');

  std::size_t backslashes = 0;
  for (const wchar_t ch : argument) {
    if (ch == L'\\') {
      ++backslashes;
      continue;
    }

    if (ch == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'"');
      backslashes = 0;
      continue;
    }

    result.append(backslashes, L'\\');
    backslashes = 0;
    result.push_back(ch);
  }

  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

std::wstring BuildCommandLine(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args) {
  std::wstring command_line = QuoteArgument(executable.wstring());

  for (std::size_t i = 1; i < args.size(); ++i) {
    const auto& arg = args[i];
    if (arg == "--theseus_runtime_child" ||
        arg == "--launcher" ||
        arg == "--no_launcher") {
      continue;
    }
    command_line.push_back(L' ');
    command_line += QuoteArgument(Utf8ToWide(arg));
  }

  command_line += L" --theseus_runtime_child --no_launcher";
  return command_line;
}

std::wstring TimeStamp() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buffer[64] = {};
  swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u_%03u",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  return buffer;
}

const char* ExitCodeName(DWORD code) {
  switch (code) {
    case 0:
      return "SUCCESS";
    case 3:
      return "CRT_ABORT_OR_EXPLICIT_EXIT_3";
    case 0xC0000005u:
      return "STATUS_ACCESS_VIOLATION";
    case 0xC0000017u:
      return "STATUS_NO_MEMORY";
    case 0xC00000FDu:
      return "STATUS_STACK_OVERFLOW";
    case 0xC0000374u:
      return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409u:
      return "STATUS_STACK_BUFFER_OVERRUN_OR_FAST_FAIL";
    case 0xC0000602u:
      return "STATUS_FAIL_FAST_EXCEPTION";
    case 0xE06D7363u:
      return "UNHANDLED_CPP_EXCEPTION";
    case kTheseusHardExitSelfTest:
      return "THESEUS_HARD_EXIT_SELF_TEST";
    default:
      return "UNKNOWN_OR_APPLICATION_DEFINED";
  }
}

bool SessionHasCleanShutdown(const std::filesystem::path& session_path) {
  std::ifstream stream(session_path, std::ios::binary);
  if (!stream) {
    return false;
  }
  std::string content(
      (std::istreambuf_iterator<char>(stream)),
      std::istreambuf_iterator<char>());
  return content.find("runtime: shutdown") != std::string::npos;
}

void WriteHardExitReport(
    const std::filesystem::path& root,
    DWORD child_pid,
    DWORD exit_code,
    bool clean_shutdown,
    std::uint64_t peak_working_set,
    std::uint64_t peak_private_usage,
    DWORD peak_handle_count) {
  std::error_code ec;
  const auto crash_dir = root / "UserData" / "Logs" / "Crashes";
  std::filesystem::create_directories(crash_dir, ec);

  const auto path =
      crash_dir / (L"TheseusHardExit_" + TimeStamp() + L"_pid" +
                   std::to_wstring(child_pid) + L".txt");

  FILE* file = nullptr;
  _wfopen_s(&file, path.c_str(), L"wb");
  if (!file) {
    return;
  }

  std::fprintf(
      file,
      "ASURA'S WRATH - THESEUS HARD EXIT REPORT\r\n"
      "========================================\r\n"
      "Milestone: T09.1-hard-exit-guard\r\n"
      "ChildPID: %lu\r\n"
      "ExitCode: 0x%08lX (%s)\r\n"
      "CleanRuntimeShutdownBreadcrumb: %s\r\n"
      "PeakWorkingSet: %llu MB\r\n"
      "PeakPrivateUsage: %llu MB\r\n"
      "PeakHandleCount: %lu\r\n"
      "\r\n"
      "Interpretation:\r\n"
      "  This report is written by the surviving launcher/guard process.\r\n"
      "  If no TheseusCrash_*.dmp exists beside it, the game process ended\r\n"
      "  through a path that bypassed the in-process unhandled-exception\r\n"
      "  filter (for example fail-fast, explicit process termination, or a\r\n"
      "  runtime/driver path that exits directly).\r\n"
      "\r\n"
      "Session log:\r\n"
      "  UserData\\Logs\\StabilitySession.txt\r\n",
      static_cast<unsigned long>(child_pid),
      static_cast<unsigned long>(exit_code),
      ExitCodeName(exit_code),
      clean_shutdown ? "yes" : "no",
      static_cast<unsigned long long>(
          peak_working_set / (1024ull * 1024ull)),
      static_cast<unsigned long long>(
          peak_private_usage / (1024ull * 1024ull)),
      static_cast<unsigned long>(peak_handle_count));

  std::fflush(file);
  std::fclose(file);
}

#endif

}  // namespace

[[noreturn]] void TriggerHardExitSelfTest() {
#if defined(_WIN32)
  TerminateProcess(GetCurrentProcess(), kTheseusHardExitSelfTest);
#endif
  std::abort();
}

bool LaunchGuardedRuntime(const std::vector<std::string>& runtime_args) {
#if defined(_WIN32)
  const auto executable = ExecutablePath();
  if (executable.empty()) {
    return false;
  }

  auto command_line = BuildCommandLine(executable, runtime_args);
  std::vector<wchar_t> mutable_command(
      command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};

  const auto root = executable.parent_path();
  const BOOL created = CreateProcessW(
      executable.c_str(),
      mutable_command.data(),
      nullptr,
      nullptr,
      FALSE,
      CREATE_UNICODE_ENVIRONMENT,
      nullptr,
      root.c_str(),
      &startup,
      &process);

  if (!created) {
    return false;
  }

  CloseHandle(process.hThread);

  std::uint64_t peak_working_set = 0;
  std::uint64_t peak_private_usage = 0;
  DWORD peak_handle_count = 0;

  while (true) {
    const DWORD wait = WaitForSingleObject(process.hProcess, 1000);
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    if (wait == WAIT_FAILED) {
      break;
    }

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            process.hProcess,
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
      peak_working_set =
          std::max<std::uint64_t>(
              peak_working_set,
              static_cast<std::uint64_t>(counters.PeakWorkingSetSize));
      peak_private_usage =
          std::max<std::uint64_t>(
              peak_private_usage,
              static_cast<std::uint64_t>(counters.PrivateUsage));
    }

    DWORD handle_count = 0;
    if (GetProcessHandleCount(process.hProcess, &handle_count)) {
      peak_handle_count = std::max(peak_handle_count, handle_count);
    }
  }

  DWORD exit_code = 0;
  GetExitCodeProcess(process.hProcess, &exit_code);

  // Capture final counters while the process object is still queryable.
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(
          process.hProcess,
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    peak_working_set =
        std::max<std::uint64_t>(
            peak_working_set,
            static_cast<std::uint64_t>(counters.PeakWorkingSetSize));
    peak_private_usage =
        std::max<std::uint64_t>(
            peak_private_usage,
            static_cast<std::uint64_t>(counters.PrivateUsage));
  }

  const auto session_path =
      root / "UserData" / "Logs" / "StabilitySession.txt";
  const bool clean_shutdown = SessionHasCleanShutdown(session_path);

  // A non-zero code is always abnormal. A zero code without the runtime
  // shutdown breadcrumb is also suspicious, because hard exits may explicitly
  // terminate the process with success.
  if (exit_code != 0 || !clean_shutdown) {
    WriteHardExitReport(
        root, process.dwProcessId, exit_code, clean_shutdown,
        peak_working_set, peak_private_usage, peak_handle_count);
  }

  CloseHandle(process.hProcess);
  return true;
#else
  (void)runtime_args;
  return false;
#endif
}

}  // namespace theseus::stability
