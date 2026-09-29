#include "platform/theseus_crash.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#endif

namespace theseus::crash {
namespace {

#if defined(_WIN32)

constexpr DWORD kTheseusTerminateException = 0xE0425409u;
constexpr DWORD kTheseusAbortException = 0xE0425410u;
constexpr DWORD kTheseusSelfTestException = 0xE0425499u;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_crash_in_progress{false};
std::filesystem::path g_crash_root;
std::filesystem::path g_session_log;

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

std::wstring TimeStamp() {
  SYSTEMTIME st{};
  GetLocalTime(&st);
  wchar_t buffer[64] = {};
  swprintf_s(buffer, L"%04u%02u%02u_%02u%02u%02u_%03u",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
  return buffer;
}

void AppendUtf8Line(const std::filesystem::path& path,
                    std::string_view text) noexcept {
  HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  if (!text.empty()) {
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
              &written, nullptr);
  }
  static constexpr char newline[] = "\r\n";
  WriteFile(file, newline, 2, &written, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

std::string Narrow(std::wstring_view wide) {
  if (wide.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

const char* ExceptionName(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_BREAKPOINT:
      return "EXCEPTION_BREAKPOINT";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION:
      return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
      return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW:
      return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_STACK_OVERFLOW:
      return "EXCEPTION_STACK_OVERFLOW";
    case kTheseusTerminateException:
      return "THESEUS_STD_TERMINATE";
    case kTheseusAbortException:
      return "THESEUS_ABORT";
    case kTheseusSelfTestException:
      return "THESEUS_CRASH_SELF_TEST";
    default:
      return "UNKNOWN_EXCEPTION";
  }
}

std::wstring ModuleForAddress(void* address) {
  if (!address) {
    return {};
  }
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(address), &module) ||
      !module) {
    return {};
  }

  std::wstring buffer(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(module, buffer.data(),
                         static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) {
    return {};
  }
  buffer.resize(length);
  return buffer;
}

void WritePeAudit() noexcept {
  HMODULE module = GetModuleHandleW(nullptr);
  if (!module) {
    return;
  }

  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    return;
  }
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
      reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    return;
  }

  char line[512] = {};
  std::snprintf(
      line, sizeof(line),
      "PE audit: machine=0x%04X PE32+=%d LAA=%d ASLR=%d NX=%d HighEntropyVA=%d "
      "StackReserve=%llu StackCommit=%llu",
      nt->FileHeader.Machine,
      nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 1 : 0,
      (nt->FileHeader.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) ? 1 : 0,
      (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE) ? 1 : 0,
      (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_NX_COMPAT) ? 1 : 0,
      (nt->OptionalHeader.DllCharacteristics & IMAGE_DLLCHARACTERISTICS_HIGH_ENTROPY_VA) ? 1 : 0,
      static_cast<unsigned long long>(nt->OptionalHeader.SizeOfStackReserve),
      static_cast<unsigned long long>(nt->OptionalHeader.SizeOfStackCommit));
  AppendUtf8Line(g_session_log, line);
}

void WriteModuleList(HANDLE file) noexcept {
  HANDLE snapshot = CreateToolhelp32Snapshot(
      TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }

  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  DWORD written = 0;

  static constexpr char header[] = "\r\nLoaded modules:\r\n";
  WriteFile(file, header, sizeof(header) - 1, &written, nullptr);

  if (Module32FirstW(snapshot, &entry)) {
    do {
      const auto path = Narrow(entry.szExePath);
      char line[1024] = {};
      const int length = std::snprintf(
          line, sizeof(line), "  0x%p  %8lu KB  %s\r\n",
          entry.modBaseAddr,
          static_cast<unsigned long>(entry.modBaseSize / 1024),
          path.c_str());
      if (length > 0) {
        WriteFile(file, line,
                  static_cast<DWORD>(std::min<int>(
                      length, static_cast<int>(sizeof(line) - 1))),
                  &written, nullptr);
      }
    } while (Module32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
}

void WriteCrashText(const std::filesystem::path& path,
                    EXCEPTION_POINTERS* exception) noexcept {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  const DWORD pid = GetCurrentProcessId();
  const DWORD tid = GetCurrentThreadId();
  const DWORD code =
      exception && exception->ExceptionRecord
          ? exception->ExceptionRecord->ExceptionCode
          : 0;
  void* address =
      exception && exception->ExceptionRecord
          ? exception->ExceptionRecord->ExceptionAddress
          : nullptr;

  const auto module_path = ModuleForAddress(address);
  const auto module_utf8 = Narrow(module_path);

  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  GlobalMemoryStatusEx(&memory);

  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  GetProcessMemoryInfo(
      GetCurrentProcess(),
      reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
      sizeof(counters));

  char text[4096] = {};
  const int length = std::snprintf(
      text, sizeof(text),
      "ASURA'S WRATH - THESEUS CRASH REPORT\r\n"
      "====================================\r\n"
      "Milestone: T09-exe-stability\r\n"
      "PID: %lu\r\n"
      "Thread: %lu\r\n"
      "Exception: 0x%08lX (%s)\r\n"
      "Address: 0x%p\r\n"
      "Module: %s\r\n"
      "LastError: %lu\r\n"
      "WorkingSet: %llu MB\r\n"
      "PrivateUsage: %llu MB\r\n"
      "SystemMemoryLoad: %lu%%\r\n"
      "AvailablePhysical: %llu MB\r\n"
      "SessionBreadcrumbs: %s\r\n",
      static_cast<unsigned long>(pid),
      static_cast<unsigned long>(tid),
      static_cast<unsigned long>(code), ExceptionName(code),
      address,
      module_utf8.empty() ? "<unknown>" : module_utf8.c_str(),
      static_cast<unsigned long>(GetLastError()),
      static_cast<unsigned long long>(
          counters.WorkingSetSize / (1024ull * 1024ull)),
      static_cast<unsigned long long>(
          counters.PrivateUsage / (1024ull * 1024ull)),
      static_cast<unsigned long>(memory.dwMemoryLoad),
      static_cast<unsigned long long>(
          memory.ullAvailPhys / (1024ull * 1024ull)),
      Narrow(g_session_log.wstring()).c_str());

  DWORD written = 0;
  if (length > 0) {
    WriteFile(file, text,
              static_cast<DWORD>(std::min<int>(
                  length, static_cast<int>(sizeof(text) - 1))),
              &written, nullptr);
  }

  if (exception && exception->ExceptionRecord) {
    const auto* record = exception->ExceptionRecord;
    char details[1024] = {};
    int details_length = std::snprintf(
        details, sizeof(details),
        "\r\nExceptionFlags: 0x%08lX\r\n"
        "Parameters: %lu\r\n",
        static_cast<unsigned long>(record->ExceptionFlags),
        static_cast<unsigned long>(record->NumberParameters));
    if (details_length > 0) {
      WriteFile(file, details,
                static_cast<DWORD>(std::min<int>(
                    details_length,
                    static_cast<int>(sizeof(details) - 1))),
                &written, nullptr);
    }

    for (DWORD i = 0; i < record->NumberParameters; ++i) {
      char parameter[128] = {};
      const int parameter_length = std::snprintf(
          parameter, sizeof(parameter), "  [%lu] 0x%llX\r\n",
          static_cast<unsigned long>(i),
          static_cast<unsigned long long>(
              record->ExceptionInformation[i]));
      if (parameter_length > 0) {
        WriteFile(file, parameter,
                  static_cast<DWORD>(parameter_length),
                  &written, nullptr);
      }
    }
  }

  WriteModuleList(file);
  FlushFileBuffers(file);
  CloseHandle(file);
}

void WriteMiniDump(const std::filesystem::path& path,
                   EXCEPTION_POINTERS* exception) noexcept {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION info{};
  info.ThreadId = GetCurrentThreadId();
  info.ExceptionPointers = exception;
  info.ClientPointers = FALSE;

  const auto type = static_cast<MINIDUMP_TYPE>(
      MiniDumpNormal |
      MiniDumpWithThreadInfo |
      MiniDumpWithUnloadedModules |
      MiniDumpWithHandleData |
      MiniDumpWithIndirectlyReferencedMemory);

  MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), file, type,
      exception ? &info : nullptr, nullptr, nullptr);
  FlushFileBuffers(file);
  CloseHandle(file);
}

LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exception) {
  if (g_crash_in_progress.exchange(true, std::memory_order_acq_rel)) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  const auto stamp = TimeStamp();
  const DWORD pid = GetCurrentProcessId();
  const DWORD tid = GetCurrentThreadId();

  wchar_t base[160] = {};
  swprintf_s(base, L"TheseusCrash_%s_pid%lu_tid%lu",
             stamp.c_str(),
             static_cast<unsigned long>(pid),
             static_cast<unsigned long>(tid));

  const auto report_path = g_crash_root / (std::wstring(base) + L".txt");
  const auto dump_path = g_crash_root / (std::wstring(base) + L".dmp");

  Breadcrumb("UNHANDLED EXCEPTION - writing crash dump");
  WriteCrashText(report_path, exception);
  WriteMiniDump(dump_path, exception);

  return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateHandler() noexcept {
  Breadcrumb("std::terminate invoked");
  RaiseException(kTheseusTerminateException, EXCEPTION_NONCONTINUABLE,
                 0, nullptr);
  TerminateProcess(GetCurrentProcess(), kTheseusTerminateException);
  std::abort();
}

void AbortHandler(int) {
  Breadcrumb("SIGABRT received");
  RaiseException(kTheseusAbortException, EXCEPTION_NONCONTINUABLE,
                 0, nullptr);
  TerminateProcess(GetCurrentProcess(), kTheseusAbortException);
}

#endif  // _WIN32

}  // namespace

void Install() {
#if defined(_WIN32)
  bool expected = false;
  if (!g_installed.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    // Reassert our process-wide filter in case a library replaced it later.
    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    return;
  }

  const auto root = ExecutableFolder();
  g_crash_root = root / "UserData" / "Logs" / "Crashes";
  g_session_log = root / "UserData" / "Logs" / "StabilitySession.txt";

  std::error_code ec;
  std::filesystem::create_directories(g_crash_root, ec);

  SetUnhandledExceptionFilter(UnhandledExceptionFilter);
  std::set_terminate(TerminateHandler);
  std::signal(SIGABRT, AbortHandler);

  {
    HANDLE file = CreateFileW(
        g_session_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
    }
  }

  Breadcrumb("Theseus crash diagnostics installed");
  WritePeAudit();
#endif
}

[[noreturn]] void TriggerSelfTestCrash() {
#if defined(_WIN32)
  Breadcrumb("CI self-test: intentional crash");
  RaiseException(kTheseusSelfTestException, EXCEPTION_NONCONTINUABLE,
                 0, nullptr);
  TerminateProcess(GetCurrentProcess(), kTheseusSelfTestException);
#else
  std::abort();
#endif
  std::abort();
}

void Breadcrumb(std::string_view message) noexcept {
#if defined(_WIN32)
  if (g_session_log.empty()) {
    return;
  }

  SYSTEMTIME st{};
  GetLocalTime(&st);
  char line[1024] = {};
  const int length = std::snprintf(
      line, sizeof(line),
      "%02u:%02u:%02u.%03u [pid=%lu tid=%lu] %.*s",
      st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
      static_cast<unsigned long>(GetCurrentProcessId()),
      static_cast<unsigned long>(GetCurrentThreadId()),
      static_cast<int>(std::min<std::size_t>(
          message.size(), sizeof(line) / 2)),
      message.data());

  if (length > 0) {
    AppendUtf8Line(
        g_session_log,
        std::string_view(line, static_cast<std::size_t>(
            std::min<int>(length, static_cast<int>(sizeof(line) - 1)))));
  }
#else
  (void)message;
#endif
}

}  // namespace theseus::crash
