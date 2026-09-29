#include "compat/rexglue_presentation_bridge.h"

#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace asura::compat {

std::unique_ptr<rex::ui::Window> CreatePresentationWindow(
    rex::ui::WindowedAppContext& context,
    std::string_view title,
    const theseus::PresentationPolicyState& policy) {
  auto window = rex::ui::Window::Create(
      context, title,
      static_cast<std::uint32_t>(policy.width),
      static_cast<std::uint32_t>(policy.height));
  if (window) {
    window->SetFullscreen(policy.fullscreen);
  }
  return window;
}

void AttachPresentationPresenter(rex::ui::Window& window,
                                 rex::ui::Presenter* presenter) {
  window.SetPresenter(presenter);
}

}  // namespace asura::compat
