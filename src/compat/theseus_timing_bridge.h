#pragma once

#include <cstdint>
#include <utility>

namespace theseus::timing {

std::uint64_t HostTickFrequency();
std::uint64_t HostTickCount();
std::uint64_t HostSystemTime();
std::uint64_t HostUptimeMillis();

double GuestTimeScalar();
void SetGuestTimeScalar(double scalar);
std::pair<std::uint64_t, std::uint64_t> GuestTickRatio();

std::uint64_t GuestTickFrequency();
void SetGuestTickFrequency(std::uint64_t frequency);

std::uint64_t GuestSystemTimeBase();
void SetGuestSystemTimeBase(std::uint64_t time_base);

std::uint64_t QueryGuestTickCount(bool no_scaling);
std::uint64_t QueryGuestSystemTime(bool no_scaling);
std::uint32_t QueryGuestUptimeMillis(bool no_scaling);
void SetGuestSystemTime(std::uint64_t system_time, bool no_scaling);

std::uint32_t ScaleGuestDurationMillis(std::uint32_t guest_ms,
                                       bool no_scaling);
std::int64_t ScaleGuestDurationFileTime(std::int64_t guest_file_time,
                                        bool no_scaling);
void ScaleGuestDurationTimeval(std::int32_t* tv_sec, std::int32_t* tv_usec,
                               bool no_scaling);

}  // namespace theseus::timing
