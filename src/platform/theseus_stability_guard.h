#pragma once

#include <string>
#include <vector>

namespace theseus::stability {

// Launches a second copy of this executable as the actual game runtime and
// waits for it from a small launcher/guard process. The child is NOT debugged,
// so runtime timing and IsDebuggerPresent behavior are unchanged.
//
// Returns true if a child process was successfully launched and monitored.
// The caller should then exit instead of starting a second runtime in-process.
bool LaunchGuardedRuntime(const std::vector<std::string>& runtime_args);

}  // namespace theseus::stability
