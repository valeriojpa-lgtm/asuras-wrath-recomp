#include "platform/theseus_graphics.h"

// T10 establishes a project-owned graphics policy boundary without replacing
// the validated Xenos renderer in one risky step. Backend selection, adapter
// preference, VSync and async-shader policy belong to Theseus. The current
// rexgpu-xenos D3D12/Vulkan plugin remains a temporary rendering compatibility
// backend until later graphics milestones can be A/B validated.
