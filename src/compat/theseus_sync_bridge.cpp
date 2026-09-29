#include "compat/theseus_sync_bridge.h"

#include <algorithm>
#include <limits>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace theseus::sync {
namespace {

#if defined(_WIN32)
DWORD TimeoutToWin32(std::uint64_t timeout_ms) noexcept {
  if (timeout_ms >= std::numeric_limits<DWORD>::max()) {
    return INFINITE;
  }
  return static_cast<DWORD>(timeout_ms);
}

WaitOutcome TranslateWait(DWORD result) noexcept {
  switch (result) {
    case WAIT_OBJECT_0:
      return WaitOutcome::kSuccess;
    case WAIT_ABANDONED:
      return WaitOutcome::kAbandoned;
    case WAIT_IO_COMPLETION:
      return WaitOutcome::kUserCallback;
    case WAIT_TIMEOUT:
      return WaitOutcome::kTimeout;
    default:
      return WaitOutcome::kFailed;
  }
}
#endif

}  // namespace

void Yield() noexcept {
#if defined(_WIN32)
  SwitchToThread();
  ::MemoryBarrier();
#endif
}

void MemoryBarrier() noexcept {
#if defined(_WIN32)
  ::MemoryBarrier();
#endif
}

void SleepMicros(std::uint64_t microseconds) noexcept {
#if defined(_WIN32)
  if (microseconds < 100) {
    Yield();
    return;
  }
  ::Sleep(static_cast<DWORD>(microseconds / 1000));
#endif
}

AlertableSleepOutcome AlertableSleepMicros(
    std::uint64_t microseconds) noexcept {
#if defined(_WIN32)
  const auto result =
      ::SleepEx(static_cast<DWORD>(microseconds / 1000), TRUE);
  return result == WAIT_IO_COMPLETION ? AlertableSleepOutcome::kAlerted
                                      : AlertableSleepOutcome::kSuccess;
#else
  return AlertableSleepOutcome::kSuccess;
#endif
}

std::uint32_t AllocateTls() {
#if defined(_WIN32)
  return static_cast<std::uint32_t>(::TlsAlloc());
#else
  return std::numeric_limits<std::uint32_t>::max();
#endif
}

bool FreeTls(std::uint32_t slot) noexcept {
#if defined(_WIN32)
  return ::TlsFree(static_cast<DWORD>(slot)) != FALSE;
#else
  return false;
#endif
}

std::uintptr_t GetTls(std::uint32_t slot) noexcept {
#if defined(_WIN32)
  return reinterpret_cast<std::uintptr_t>(
      ::TlsGetValue(static_cast<DWORD>(slot)));
#else
  return 0;
#endif
}

bool SetTls(std::uint32_t slot, std::uintptr_t value) noexcept {
#if defined(_WIN32)
  return ::TlsSetValue(static_cast<DWORD>(slot),
                       reinterpret_cast<void*>(value)) != FALSE;
#else
  return false;
#endif
}

void CloseNativeHandle(void* handle) noexcept {
#if defined(_WIN32)
  if (handle) {
    ::CloseHandle(static_cast<HANDLE>(handle));
  }
#endif
}

WaitOutcome WaitOne(void* handle, bool alertable,
                    std::uint64_t timeout_ms) noexcept {
#if defined(_WIN32)
  return TranslateWait(::WaitForSingleObjectEx(
      static_cast<HANDLE>(handle), TimeoutToWin32(timeout_ms),
      alertable ? TRUE : FALSE));
#else
  return WaitOutcome::kFailed;
#endif
}

WaitOutcome SignalAndWait(void* signal_handle, void* wait_handle,
                          bool alertable, std::uint64_t timeout_ms) noexcept {
#if defined(_WIN32)
  return TranslateWait(::SignalObjectAndWait(
      static_cast<HANDLE>(signal_handle), static_cast<HANDLE>(wait_handle),
      TimeoutToWin32(timeout_ms), alertable ? TRUE : FALSE));
#else
  return WaitOutcome::kFailed;
#endif
}

WaitManyResult WaitMany(void* const* handles, std::size_t count,
                        bool wait_all, bool alertable,
                        std::uint64_t timeout_ms) noexcept {
#if defined(_WIN32)
  if (!handles || !count || count > MAXIMUM_WAIT_OBJECTS) {
    return {};
  }

  std::vector<HANDLE> native(count);
  for (std::size_t i = 0; i < count; ++i) {
    native[i] = static_cast<HANDLE>(handles[i]);
  }

  const DWORD result = ::WaitForMultipleObjectsEx(
      static_cast<DWORD>(native.size()), native.data(),
      wait_all ? TRUE : FALSE, TimeoutToWin32(timeout_ms),
      alertable ? TRUE : FALSE);

  if (result >= WAIT_OBJECT_0 &&
      result < WAIT_OBJECT_0 + native.size()) {
    return {WaitOutcome::kSuccess,
            static_cast<std::size_t>(result - WAIT_OBJECT_0)};
  }
  if (result >= WAIT_ABANDONED_0 &&
      result < WAIT_ABANDONED_0 + native.size()) {
    return {WaitOutcome::kAbandoned,
            static_cast<std::size_t>(result - WAIT_ABANDONED_0)};
  }
  return {TranslateWait(result), 0};
#else
  return {};
#endif
}

void* CreateEvent(bool manual_reset, bool initial_state) noexcept {
#if defined(_WIN32)
  return ::CreateEventW(nullptr, manual_reset ? TRUE : FALSE,
                        initial_state ? TRUE : FALSE, nullptr);
#else
  return nullptr;
#endif
}

bool SetEventSignaled(void* handle) noexcept {
#if defined(_WIN32)
  return ::SetEvent(static_cast<HANDLE>(handle)) != FALSE;
#else
  return false;
#endif
}

bool ResetEventSignaled(void* handle) noexcept {
#if defined(_WIN32)
  return ::ResetEvent(static_cast<HANDLE>(handle)) != FALSE;
#else
  return false;
#endif
}

bool PulseEventSignaled(void* handle) noexcept {
#if defined(_WIN32)
  return ::PulseEvent(static_cast<HANDLE>(handle)) != FALSE;
#else
  return false;
#endif
}

void* CreateSemaphore(std::int32_t initial_count,
                      std::int32_t maximum_count) noexcept {
#if defined(_WIN32)
  return ::CreateSemaphoreW(nullptr, initial_count, maximum_count, nullptr);
#else
  return nullptr;
#endif
}

bool ReleaseSemaphore(void* handle, std::int32_t release_count,
                      std::int32_t* previous_count) noexcept {
#if defined(_WIN32)
  return ::ReleaseSemaphore(
             static_cast<HANDLE>(handle), release_count,
             reinterpret_cast<LPLONG>(previous_count)) != FALSE;
#else
  return false;
#endif
}

void* CreateMutex(bool initial_owner) noexcept {
#if defined(_WIN32)
  return ::CreateMutexW(nullptr, initial_owner ? TRUE : FALSE, nullptr);
#else
  return nullptr;
#endif
}

bool ReleaseMutex(void* handle) noexcept {
#if defined(_WIN32)
  return ::ReleaseMutex(static_cast<HANDLE>(handle)) != FALSE;
#else
  return false;
#endif
}

void* CreateWaitableTimer(bool manual_reset) noexcept {
#if defined(_WIN32)
  return ::CreateWaitableTimerW(nullptr, manual_reset ? TRUE : FALSE, nullptr);
#else
  return nullptr;
#endif
}

bool SetWaitableTimer(void* handle, std::int64_t due_time_filetime,
                      std::int32_t period_ms, std::uintptr_t completion_routine,
                      void* completion_context) noexcept {
#if defined(_WIN32)
  LARGE_INTEGER due_time{};
  due_time.QuadPart = due_time_filetime;
  return ::SetWaitableTimer(
             static_cast<HANDLE>(handle), &due_time, period_ms,
             reinterpret_cast<PTIMERAPCROUTINE>(completion_routine),
             completion_context, FALSE) != FALSE;
#else
  return false;
#endif
}

bool CancelWaitableTimer(void* handle) noexcept {
#if defined(_WIN32)
  return ::CancelWaitableTimer(static_cast<HANDLE>(handle)) != FALSE;
#else
  return false;
#endif
}

}  // namespace theseus::sync
