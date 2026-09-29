#pragma once

#include "platform/theseus_graphics.h"

namespace rex {
struct RuntimeConfig;
}

namespace asura::compat {

// Temporary T10 bridge: creates the validated rexgpu-xenos backend selected by
// the Theseus-owned graphics policy, then injects it into RuntimeConfig.
// Returning false leaves the caller free to use the preserved legacy path.
bool InstallGraphicsBackend(rex::RuntimeConfig& config,
                            const theseus::GraphicsPolicyState& policy);

}  // namespace asura::compat
