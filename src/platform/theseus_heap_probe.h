#pragma once

namespace theseus::heap_probe {

// Configures the AddressSanitizer child environment for T09.2.
// No-op in normal non-probe builds.
void ConfigureChildEnvironment() noexcept;

// CI/developer-only self-test. In an ASan build this intentionally performs a
// small heap out-of-bounds write so the sanitizer path can be validated.
[[noreturn]] void TriggerAsanSelfTest();

}  // namespace theseus::heap_probe
