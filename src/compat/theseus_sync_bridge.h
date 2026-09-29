#pragma once

#include <cstddef>
#include <cstdint>

namespace theseus::sync {

enum class WaitOutcome : std::uint8_t {
  kSuccess,
  kUserCallback,
  kTimeout,
  kAbandoned,
  kFailed,
};

struct WaitManyResult {
  WaitOutcome outcome = WaitOutcome::kFailed;
  std::size_t index = 0;
};

enum class AlertableSleepOutcome : std::uint8_t {
  kSuccess,
  kAlerted,
};

void Yield() noexcept;
void MemoryBarrier() noexcept;
void SleepMicros(std::uint64_t microseconds) noexcept;
AlertableSleepOutcome AlertableSleepMicros(std::uint64_t microseconds) noexcept;

std::uint32_t AllocateTls();
bool FreeTls(std::uint32_t slot) noexcept;
std::uintptr_t GetTls(std::uint32_t slot) noexcept;
bool SetTls(std::uint32_t slot, std::uintptr_t value) noexcept;

void CloseNativeHandle(void* handle) noexcept;

WaitOutcome WaitOne(void* handle, bool alertable,
                    std::uint64_t timeout_ms) noexcept;
WaitOutcome SignalAndWait(void* signal_handle, void* wait_handle,
                          bool alertable, std::uint64_t timeout_ms) noexcept;
WaitManyResult WaitMany(void* const* handles, std::size_t count,
                        bool wait_all, bool alertable,
                        std::uint64_t timeout_ms) noexcept;

void* CreateEvent(bool manual_reset, bool initial_state) noexcept;
bool SetEventSignaled(void* handle) noexcept;
bool ResetEventSignaled(void* handle) noexcept;
bool PulseEventSignaled(void* handle) noexcept;

void* CreateSemaphore(std::int32_t initial_count,
                      std::int32_t maximum_count) noexcept;
bool ReleaseSemaphore(void* handle, std::int32_t release_count,
                      std::int32_t* previous_count) noexcept;

void* CreateMutex(bool initial_owner) noexcept;
bool ReleaseMutex(void* handle) noexcept;

void* CreateWaitableTimer(bool manual_reset) noexcept;
bool SetWaitableTimer(void* handle, std::int64_t due_time_filetime,
                      std::int32_t period_ms, std::uintptr_t completion_routine,
                      void* completion_context) noexcept;
bool CancelWaitableTimer(void* handle) noexcept;

}  // namespace theseus::sync
