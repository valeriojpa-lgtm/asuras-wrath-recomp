#pragma once

#include <string_view>

namespace theseus::crash {

// Idempotent, process-wide crash diagnostics. On Windows this installs an
// unhandled-exception filter and creates the portable crash-log directory.
void Install();

// Appends a lightweight timestamped breadcrumb to the current session log.
void Breadcrumb(std::string_view message) noexcept;

// CI/developer-only end-to-end validation. This intentionally terminates the
// process through the installed exception filter.
[[noreturn]] void TriggerSelfTestCrash();

}  // namespace theseus::crash
