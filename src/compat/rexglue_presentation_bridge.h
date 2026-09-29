#pragma once

#include <memory>
#include <string_view>

#include "platform/theseus_presentation.h"

namespace rex::ui {
class Presenter;
class Window;
class WindowedAppContext;
}

namespace asura::compat {

std::unique_ptr<rex::ui::Window> CreatePresentationWindow(
    rex::ui::WindowedAppContext& context,
    std::string_view title,
    const theseus::PresentationPolicyState& policy);

void AttachPresentationPresenter(rex::ui::Window& window,
                                 rex::ui::Presenter* presenter);

void DetachPresentationPresenter(rex::ui::Window& window);

}  // namespace asura::compat
