#pragma once

#include "core/dsl.h"

#include <functional>

namespace core::debug {

enum class DockPosition { Floating, Left, Bottom, Right };

struct DevtoolsUiState {
    float width = 0.0f;
    float height = 0.0f;
    Rect panel;
    bool detached = false;
    DockPosition dockPosition = DockPosition::Bottom;
    bool moreMenuOpen = false;
};

struct DevtoolsUiActions {
    std::function<void()> toggleMoreMenu;
    std::function<void()> close;
    std::function<void(DockPosition)> selectDockPosition;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace core::debug
