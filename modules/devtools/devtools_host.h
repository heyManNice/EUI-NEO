#pragma once

#if defined(EUI_DEBUG_BUILD)

#include "core/dsl_runtime.h"
#include "eui/detail/overlay_host.h"
#include "modules/devtools/devtools_ui.h"

#include <deque>
#include <functional>

namespace modules::devtools {

// Attaches or detaches the panel overlay. Called by Session.
void attachDevtoolsHost();
void detachDevtoolsHost();

// Debug panel host. It owns the panel Runtime, the dock geometry, the detached
// window and the input the page must not see. This is the only part of the
// panel that talks to the framework, and only through app::detail::OverlayHost.
class DevtoolsHost : public app::detail::OverlayHost {
public:
    bool handleHotkey(const core::KeyEvent& key) override;
    core::Rect contentBounds() const override;
    void filterInput(std::vector<core::PointerEvent>& pointerEvents, core::ScrollEvent& scrollEvent) override;
    bool update(int framebufferWidth, int framebufferHeight, float dpiScale, float deltaSeconds) override;
    void render(int windowWidth, int windowHeight, float dpiScale, const core::Rect* dirtyRect) override;
    void setPerformanceSnapshot(const app::PerformanceSnapshot& snapshot) override;
    bool wantsElementTree() const override;
    void setElementTree(const core::dsl::runtime::ElementTreeSnapshot& tree) override;
    const std::string& hoveredElement() const override;
    bool pickingElement() const override;
    core::PointerEvent pickedPointer() const override;
    void setElementUnderPointer(const std::string& id) override;
    const std::string& propertiesElement() const override;
    void setElementProperties(const core::dsl::runtime::DebugElementProperties& properties) override;
    bool takeElementPropertyEdit(ElementPropertyEdit& edit) override;
    void setElementPropertyOverrideCount(std::size_t count) override;
    void releaseGraphicsResources() override;
    void shutdown() override;

    void describeDetachedWindow(app::detail::DetachedWindowOptions& options) const override;
    void setDetachedWindowOpener(std::function<void()> opener) override;
    void setDetachedWindowCloser(std::function<void()> closer) override;
    void composeDetached(core::dsl::Ui& ui, const core::dsl::Screen& screen) override;
    void detachedWindowClosed() override;

    bool visible() const;
    DockPosition dockPosition() const;
    DevtoolsTab activeTab() const;
    int contentHeight() const;
    float performanceScrollOffset() const;
    const std::string& selectedElement() const;
    const std::vector<std::string>& expandedElements() const;
    const core::dsl::runtime::ElementTreeSnapshot& elementTree() const;
    const core::dsl::runtime::DebugElementProperties& properties() const;
    std::size_t propertyOverrideCount() const;

    // The panel's own element tree. Tests use it to see what the panel composed
    // without a renderer; it is also what a future "inspect the inspector" view
    // would read.
    core::dsl::runtime::ElementTreeSnapshot panelElementTree() const;

private:
    int panelSize() const;
    int minimumPanelSize() const;
    int maximumPanelSize() const;
    core::Rect panelBounds() const;
    void selectDockPosition(DockPosition position);
    void selectTab(DevtoolsTab tab);
    void setPickingElement(bool picking);
    void dismissMoreMenu();
    void close();
    void openDetachedWindow();
    void closeDetachedWindow();
    void composeUi(core::dsl::Ui& ui, float width, float height, const core::Rect& panel, bool detached);
    void requestCompose();
    void queueElementPropertyEdit(const ElementPropertyEdit& edit);
    bool overResizeBoundary(double x, double y) const;

    core::dsl::Runtime runtime_;
    DevtoolsPanelState* panelState_ = nullptr;
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    float dpiScale_ = 1.0f;
    float panelHeightLogical_ = 0.0f;
    float panelWidthLogical_ = 0.0f;
    double dragStartX_ = 0.0;
    double dragStartY_ = 0.0;
    int dragStartSize_ = 0;
    bool resizing_ = false;
    // The panel edge owns the pointer: it is what takes a press that resizes the
    // panel away from the panel's own controls.
    bool panelEdgeActive_ = false;
    // Picking: the pointer the panel wants answered, the element it landed on, and
    // whether the next answer commits it as the selection.
    core::PointerEvent pickedPointer_;
    std::string elementUnderPointer_;
    bool pickCommitPending_ = false;
    bool visible_ = false;
    DockPosition dockPosition_ = DockPosition::Bottom;
    app::PerformanceSnapshot performanceSnapshot_;
    core::dsl::runtime::ElementTreeSnapshot elementTree_;
    core::dsl::runtime::DebugElementProperties properties_;
    std::deque<ElementPropertyEdit> propertyEdits_;
    std::size_t propertyOverrideCount_ = 0;
    std::function<void()> detachedWindowOpener_;
    std::function<void()> detachedWindowCloser_;
    bool composeRequested_ = true;
};

} // namespace modules::devtools

#endif
