#pragma once

#include <filesystem>

#include "platform/theseus_filesystem.h"
#include "platform/theseus_platform.h"

namespace rex {
class Runtime;
}

namespace asura::compat {

// Temporary compatibility adapter: exposes clean PC-side files through the
// guest Xbox paths still expected by the recompiled game.
bool MountPcDataLayout(rex::Runtime* runtime,
                       const std::filesystem::path& game_data_root,
                       const std::filesystem::path& user_data_root,
                       const theseus::PortablePaths& portable_paths,
                       const theseus::NativeFileSystem& files);

// Legacy import path only. T03's runtime filesystem is native on the host, but
// ISO/GDFX parsing still reuses ReXGlue's disc-image parser until replaced.
bool ExtractDiscImageToDirectory(
    const std::filesystem::path& image_path,
    const std::filesystem::path& destination,
    const theseus::NativeFileSystem& files);

}  // namespace asura::compat
