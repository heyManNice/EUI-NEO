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
    // The row the pointer is over, previewed in the page until it leaves.
    std::string hoveredElement;
    float propertiesScrollOffset = 0.0f;
    // Height of the property area. The host seeds it from the theme on the first
    // composition and the divider at its top edge changes it from then on. The drag
    // start height is what the divider measures its pointer against.
    float propertiesHeight = 0.0f;
    float propertiesResizeStartHeight = 0.0f;
    // The colour whose channels are open under its row; one at a time keeps the
    // property list flat and short.
    bool colorEditorOpen = false;
    core::dsl::runtime::DebugPropertyId colorEditorProperty = core::dsl::runtime::DebugPropertyId::Color;
    // Elements the user opened. The tree itself comes from the app, so the panel
    // only remembers what the user expanded inside it: every node with children
    // starts collapsed, which keeps a deep page readable from the first frame.
    std::vector<std::string> expandedElements;
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
    // Values of the selected element, owned by the host and read once per refresh.
    const core::dsl::runtime::DebugElementProperties* properties = nullptr;
    std::size_t propertyOverrideCount = 0;
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
    std::function<void(const std::string&, bool)> hoverElement;
    std::function<void(const std::string&)> toggleElementCollapsed;
    std::function<void(const std::string&)> copyElementId;
    std::function<void(float)> setPropertiesScrollOffset;
    std::function<void(float)> setPropertiesHeight;
    std::function<void()> beginPropertiesResize;
    std::function<void(core::dsl::runtime::DebugPropertyId, bool)> togglePropertyColorEditor;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, float)> setElementPropertyNumber;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, const core::Color&)> setElementPropertyColor;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, bool)> setElementPropertyFlag;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId)> clearElementProperty;
    std::function<void()> clearElementProperties;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
