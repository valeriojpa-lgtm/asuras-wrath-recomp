#include "compat/theseus_timing_bridge.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <numeric>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace theseus::timing {
namespace {

std::uint64_t Gcd(std::uint64_t a, std::uint64_t b) {
  return std::gcd(a, b);
}

void Reduce(std::uint64_t& numerator, std::uint64_t& denominator) {
  const auto divisor = Gcd(numerator, denominator);
  if (divisor) {
    numerator /= divisor;
    denominator /= divisor;
  }
}

struct ClockState {
  std::mutex mutex;
  std::atomic<double> scalar{1.0};
  std::uint64_t guest_frequency = 50000000ull;
  std::pair<std::uint64_t, std::uint64_t> tick_ratio{1, 1};
  std::uint64_t system_time_base = 0;
  std::uint64_t last_guest_tick = 0;
  std::uint64_t last_host_tick = 0;
};

ClockState& State() {
  static ClockState state;
  static std::once_flag once;
  std::call_once(once, [&] {
    state.system_time_base = HostSystemTime();
    state.last_host_tick = HostTickCount();

    std::uint64_t numerator = state.guest_frequency;
    std::uint64_t denominator = HostTickFrequency();
    Reduce(numerator, denominator);
    state.tick_ratio = {numerator, denominator};
  });
  return state;
}

void RecomputeRatioLocked(ClockState& state) {
  std::uint64_t numerator = state.guest_frequency;
  std::uint64_t denominator = HostTickFrequency();

  const double scalar = state.scalar.load(std::memory_order_relaxed);
  if (scalar > 1.0) {
    numerator *= static_cast<std::uint64_t>(scalar * 10.0);
    denominator *= 10;
  } else {
    numerator *= 10;
    denominator *= static_cast<std::uint64_t>(10.0 / scalar);
  }

  Reduce(numerator, denominator);
  state.tick_ratio = {numerator, denominator};
}

std::uint64_t UpdateGuestClock(bool no_scaling) {
  auto& state = State();
  const auto host_tick = HostTickCount();

  if (no_scaling) {
    std::lock_guard lock(state.mutex);
    return host_tick * state.tick_ratio.first / state.tick_ratio.second;
  }

  std::unique_lock lock(state.mutex, std::defer_lock);
  if (lock.try_lock()) {
    const auto delta =
        host_tick > state.last_host_tick ? host_tick - state.last_host_tick : 0;
    state.last_host_tick = host_tick;
    state.last_guest_tick +=
        delta * state.tick_ratio.first / state.tick_ratio.second;
    return state.last_guest_tick;
  }

  lock.lock();
  return state.last_guest_tick;
}

std::uint64_t GuestSystemTimeOffset(bool no_scaling) {
  auto& state = State();
  if (no_scaling) {
    std::lock_guard lock(state.mutex);
    return HostSystemTime() - state.system_time_base;
  }

  const auto guest_tick = UpdateGuestClock(false);
  std::uint64_t numerator = 10000000ull;
  std::uint64_t denominator;
  {
    std::lock_guard lock(state.mutex);
    denominator = state.guest_frequency;
  }
  Reduce(numerator, denominator);
  return guest_tick * numerator / denominator;
}

}  // namespace

std::uint64_t HostTickFrequency() {
#if defined(_WIN32)
  LARGE_INTEGER frequency{};
  QueryPerformanceFrequency(&frequency);
  return static_cast<std::uint64_t>(frequency.QuadPart);
#else
  return 1000000000ull;
#endif
}

std::uint64_t HostTickCount() {
#if defined(_WIN32)
  LARGE_INTEGER counter{};
  return QueryPerformanceCounter(&counter)
             ? static_cast<std::uint64_t>(counter.QuadPart)
             : 0ull;
#else
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}

std::uint64_t HostSystemTime() {
#if defined(_WIN32)
  FILETIME value{};
  GetSystemTimeAsFileTime(&value);
  return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32) |
         value.dwLowDateTime;
#else
  constexpr std::uint64_t kUnixToFileTime = 116444736000000000ull;
  const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count() /
                     100;
  return kUnixToFileTime + static_cast<std::uint64_t>(ticks);
#endif
}

std::uint64_t HostUptimeMillis() {
  return HostTickCount() * 1000ull / HostTickFrequency();
}

double GuestTimeScalar() {
  return State().scalar.load(std::memory_order_relaxed);
}

void SetGuestTimeScalar(double scalar) {
  if (!(scalar > 0.0)) {
    scalar = 1.0;
  }
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.scalar.store(scalar, std::memory_order_relaxed);
  RecomputeRatioLocked(state);
}

std::pair<std::uint64_t, std::uint64_t> GuestTickRatio() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return state.tick_ratio;
}

std::uint64_t GuestTickFrequency() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return state.guest_frequency;
}

void SetGuestTickFrequency(std::uint64_t frequency) {
  if (!frequency) {
    frequency = 50000000ull;
  }
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.guest_frequency = frequency;
  RecomputeRatioLocked(state);
}

std::uint64_t GuestSystemTimeBase() {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  return state.system_time_base;
}

void SetGuestSystemTimeBase(std::uint64_t time_base) {
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.system_time_base = time_base;
}

std::uint64_t QueryGuestTickCount(bool no_scaling) {
  return UpdateGuestClock(no_scaling);
}

std::uint64_t QueryGuestSystemTime(bool no_scaling) {
  auto& state = State();
  const auto offset = GuestSystemTimeOffset(no_scaling);
  std::lock_guard lock(state.mutex);
  return state.system_time_base + offset;
}

std::uint32_t QueryGuestUptimeMillis(bool no_scaling) {
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      GuestSystemTimeOffset(no_scaling) / 10000ull,
      std::numeric_limits<std::uint32_t>::max()));
}

void SetGuestSystemTime(std::uint64_t system_time, bool no_scaling) {
  if (no_scaling) {
    return;
  }
  const auto offset = GuestSystemTimeOffset(false);
  auto& state = State();
  std::lock_guard lock(state.mutex);
  state.system_time_base = system_time - offset;
}

std::uint32_t ScaleGuestDurationMillis(std::uint32_t guest_ms,
                                       bool no_scaling) {
  if (no_scaling) {
    return guest_ms;
  }
  constexpr std::uint64_t max = std::numeric_limits<std::uint32_t>::max();
  if (guest_ms >= max) {
    return static_cast<std::uint32_t>(max);
  }
  if (!guest_ms) {
    return 0;
  }
  const auto scaled = static_cast<std::uint64_t>(
      static_cast<double>(guest_ms) * GuestTimeScalar());
  return static_cast<std::uint32_t>(std::min(scaled, max));
}

std::int64_t ScaleGuestDurationFileTime(std::int64_t guest_file_time,
                                        bool no_scaling) {
  if (no_scaling || !guest_file_time) {
    return guest_file_time;
  }

  const double scalar = GuestTimeScalar();
  if (guest_file_time > 0) {
    const auto guest_time = QueryGuestSystemTime(false);
    const auto relative =
        guest_file_time - static_cast<std::int64_t>(guest_time);
    return static_cast<std::int64_t>(guest_time) +
           static_cast<std::int64_t>(relative * scalar);
  }

  return static_cast<std::int64_t>(
      static_cast<std::uint64_t>(guest_file_time) * scalar);
}

void ScaleGuestDurationTimeval(std::int32_t* tv_sec, std::int32_t* tv_usec,
                               bool no_scaling) {
  if (no_scaling || !tv_sec || !tv_usec) {
    return;
  }

  const double scalar = GuestTimeScalar();
  std::uint64_t scaled_sec =
      static_cast<std::uint64_t>(static_cast<double>(*tv_sec) * scalar);
  std::uint64_t scaled_usec =
      static_cast<std::uint64_t>(static_cast<double>(*tv_usec) * scalar);

  if (scaled_usec > std::numeric_limits<std::uint32_t>::max()) {
    const auto overflow = scaled_usec / 1000000ull;
    scaled_usec -= overflow * 1000000ull;
    scaled_sec += overflow;
  }

  *tv_sec = static_cast<std::int32_t>(scaled_sec);
  *tv_usec = static_cast<std::int32_t>(scaled_usec);
}

}  // namespace theseus::timing
