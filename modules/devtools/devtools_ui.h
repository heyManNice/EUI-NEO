#pragma once

#include "core/app/performance_snapshot.h"
#include "core/dsl.h"
#include "core/runtime/runtime_inspector.h"

#include <functional>
#include <string>
#include <vector>

namespace modules::devtools {

enum class DockPosition { Floating, Left, Bottom, Right };

// Tabs the panel shows. Performance and Elements have content; every other tab is a
// name the panel reserves for the panel it promises, and says so while it has nothing
// to show yet. What each planned panel will read is written down in the module README.
enum class DevtoolsTab {
    Performance,
    Elements,
    State,
    Input,
    Frames,
    Layout,
    Animations,
    Resources,
    Windows,
    Scale
};

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
    // The panel picks elements on the page while this is set: the pointer belongs to
    // the panel, and the element it lands on becomes the selection.
    bool pickingElement = false;
    // The selection the tree has already brought into view. A new one opens its
    // ancestors and scrolls to its row once, so collapsing or scrolling by hand
    // afterwards stays the user's choice.
    std::string revealedSelection;
    float propertiesScrollOffset = 0.0f;
    // Height of the property area, in the logical units the panel composes in. The
    // host only seeds it from the theme's fraction; the divider on its top edge owns
    // it from then on.
    float propertiesHeight = 0.0f;
    // What the drag in progress measured when it started: the height it works from
    // and the framebuffer pixels per logical unit it converts pointer deltas with.
    // Both are zeroed when the drag ends, because a pointer event is not in the
    // units the panel composes in.
    float propertiesResizeStartHeight = 0.0f;
    float propertiesResizeScale = 1.0f;
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
    // Turns picking on or off. While it is on the panel owns the pointer and the
    // element it picks becomes the selection, which the tree then keeps in view.
    std::function<void()> toggleElementPicker;
    std::function<void(const std::string&)> toggleElementCollapsed;
    std::function<void(const std::string&)> setRevealedSelection;
    std::function<void(const std::string&)> copyElementId;
    std::function<void(float)> setPropertiesScrollOffset;
    std::function<void(float)> setPropertiesHeight;
    // Starts a divider drag: the height it works from and the framebuffer pixels per
    // logical unit it converts pointer deltas with, both measured on the pressed
    // element. Pointer events arrive in framebuffer pixels while the panel composes
    // in logical units, so the ratio is what keeps a drag on the pointer instead of
    // ahead of it. The end of a drag drops both, so the next one starts from the
    // height the panel is at then instead of the one an earlier drag left behind.
    std::function<void(float, float)> beginPropertiesResize;
    std::function<void()> endPropertiesResize;
    std::function<void(core::dsl::runtime::DebugPropertyId, bool)> togglePropertyColorEditor;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, float)> setElementPropertyNumber;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, const core::Color&)> setElementPropertyColor;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId, bool)> setElementPropertyFlag;
    std::function<void(const std::string&, core::dsl::runtime::DebugPropertyId)> clearElementProperty;
    std::function<void()> clearElementProperties;
};

void composeDevtoolsUi(core::dsl::Ui& ui, const DevtoolsUiState& state, const DevtoolsUiActions& actions);

} // namespace modules::devtools
