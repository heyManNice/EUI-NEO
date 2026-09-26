#pragma once

#include "core/app/performance_snapshot.h"
#include "core/dsl.h"
#include "core/runtime/runtime_inspector.h"

#include <functional>
#include <string>
#include <vector>

namespace modules::devtools {

enum class DockPosition { Floating, Left, Bottom, Right };
enum class DevtoolsTab { Performance, Elements };

// Panel state lives in the overlay Runtime state store, so it follows the panel
// Runtime lifetime instead of a process-wide object.
struct DevtoolsPanelState {
    DevtoolsTab activeTab = DevtoolsTab::Performance;
    bool moreMenuOpen = false;
    float performanceScrollOffset = 0.0f;
    float elementsScrollOffset = 0.0f;
    std::string selectedElement;
    // Elements the user collapsed. The tree itself comes from the app, so the
    // panel only remembers what the user hid inside it.
    std::vector<std::string> collapsedElements;
};

// Everything the panel needs for one composition. The host owns geometry, the
// performance snapshot and the element tree; the panel only reads them.
struct DevtoolsUiState {
    float width = 0.0f;
    float height = 0.0f;
    core::Rect panel;
    bool detached = false;
    DockPosition dockPosition = DockPosition::Bottom;
    const DevtoolsPanelState* panelState = nullptr;
    const app::PerformanceSnapshot* performance = nullptr;
    const core::dsl::runtime::ElementTreeSnapshot* elementTree = nullptr;
};

// Commands the panel can request. None of them own state or draw.
struct DevtoolsUiActions {
    std::function<void(DevtoolsTab)> selectTab;
    std::function<void(DockPosition)> selectDockPosition;
    std::function<void()> toggleMoreMenu;
    std::function<void()> dismissMoreMenu;
    std::function<void()> close;
    std::function<void(float)> setPerformanceScrollOffset;
    std::function<void(float)> setElementsScrollOffset;
    std::function<void(const std::string&)> selectElement;
    std::function<void(const std::string&)> toggleElementCollapsed;
    std::function<void(const std::string&)> copyElementId;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
