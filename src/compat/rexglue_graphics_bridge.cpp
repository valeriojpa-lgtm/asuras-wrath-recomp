#include "compat/rexglue_graphics_bridge.h"

#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>

namespace asura::compat {

bool InstallGraphicsBackend(rex::RuntimeConfig& config,
                            const theseus::GraphicsPolicyState& policy) {
  if (config.graphics) {
    return true;
  }

  const char* backend =
      policy.backend == theseus::GraphicsBackend::kVulkan ? "vulkan" : "d3d12";

  config.graphics = rex::system::LoadGpuPlugin("xenos", backend);
  if (!config.graphics) {
    return false;
  }

  // Prevent ReXApp from loading the plugin a second time. From this point the
  // concrete renderer instance is explicitly injected by the Theseus bridge.
  config.gpu_plugin.clear();
  return true;
}

}  // namespace asura::compat
