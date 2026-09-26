#pragma once

#include "core/app/performance_snapshot.h"
#include "core/dsl.h"

#include <functional>

namespace modules::devtools {

enum class DockPosition { Floating, Left, Bottom, Right };
enum class DevtoolsTab { Performance, Elements };

// Panel state lives in the overlay Runtime state store, so it follows the panel
// Runtime lifetime instead of a process-wide object.
struct DevtoolsPanelState {
    DevtoolsTab activeTab = DevtoolsTab::Performance;
    bool moreMenuOpen = false;
    float performanceScrollOffset = 0.0f;
};

// Everything the panel needs for one composition. The host owns geometry and
// the performance snapshot; the panel only reads them.
struct DevtoolsUiState {
    float width = 0.0f;
    float height = 0.0f;
    core::Rect panel;
    bool detached = false;
    DockPosition dockPosition = DockPosition::Bottom;
    const DevtoolsPanelState* panelState = nullptr;
    const app::PerformanceSnapshot* performance = nullptr;
};

// Commands the panel can request. None of them own state or draw.
struct DevtoolsUiActions {
    std::function<void(DevtoolsTab)> selectTab;
    std::function<void(DockPosition)> selectDockPosition;
    std::function<void()> toggleMoreMenu;
    std::function<void()> dismissMoreMenu;
    std::function<void()> close;
    std::function<void(float)> setPerformanceScrollOffset;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
