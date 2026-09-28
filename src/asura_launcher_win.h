#pragma once

#if defined(_WIN32) && !defined(__ANDROID__)

#include <string>
#include <vector>

namespace asura {

// Shows the native launcher when configured to do so, persists the selected
// settings, and appends the corresponding ReXGlue command-line arguments.
// Returns false when the user chooses Exit / closes the launcher.
bool RunNativeLauncher(std::vector<std::string>& args);

}  // namespace asura

#endif
